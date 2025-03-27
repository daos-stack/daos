/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_core.h"
#include "mercury_private.h"

#include "mercury_atomic_queue.h"
#include "mercury_error.h"
#include "mercury_event.h"
#include "mercury_hash_table.h"
#include "mercury_mem.h"
#include "mercury_param.h"
#include "mercury_poll.h"
#include "mercury_thread_condition.h"
#include "mercury_thread_mutex.h"
#include "mercury_thread_pool.h"
#include "mercury_thread_rwlock.h"
#include "mercury_thread_spin.h"
#include "mercury_time.h"

#ifdef NA_HAS_SM
#    include <na_sm.h>
#endif

#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

/* Private flags */
#define HG_CORE_NO_RESPONSE  (1 << 1) /* No response required */
#define HG_CORE_SELF_FORWARD (1 << 2) /* Forward to self */

/* Size of comletion queue used for holding completed requests */
#define HG_CORE_ATOMIC_QUEUE_SIZE (1024)

/* Pre-posted requests and op IDs */
#define HG_CORE_POST_INIT          (512)
#define HG_CORE_POST_INCR          (512)
#define HG_CORE_BULK_OP_INIT_COUNT (256)

/* Number of multi-recv buffer pre-posted */
#define HG_CORE_MULTI_RECV_OP_COUNT (4)

/* Timeout on finalize */
#define HG_CORE_CLEANUP_TIMEOUT (5000)

/* Max number of events for progress */
#define HG_CORE_MAX_EVENTS (1)

/* 32-bit lock value for serial progress */
#define HG_CORE_PROGRESS_LOCK (0x80000000)

#ifdef NA_HAS_SM
/* Addr string format */
#    define HG_CORE_ADDR_MAX_SIZE      (256)
#    define HG_CORE_PROTO_DELIMITER    ":"
#    define HG_CORE_ADDR_DELIMITER     "#"
#    define HG_CORE_ADDR_DELIMITER_LEN (1)
#endif

/* Handle flags */
#define HG_CORE_HANDLE_LISTEN          (1 << 1) /* Listener handle */
#define HG_CORE_HANDLE_MULTI_RECV      (1 << 2) /* Handle used for multi-recv */
#define HG_CORE_HANDLE_USER            (1 << 3) /* User-created handle */
#define HG_CORE_HANDLE_MULTI_RECV_COPY (1 << 4) /* Copy on multi-recv */

/* Op status bits */
#define HG_CORE_OP_COMPLETED  (1 << 0) /* Operation completed */
#define HG_CORE_OP_CANCELED   (1 << 1) /* Operation canceled */
#define HG_CORE_OP_POSTED     (1 << 2) /* Operation posted (forward/respond) */
#define HG_CORE_OP_ERRORED    (1 << 3) /* Operation encountered error */
#define HG_CORE_OP_QUEUED     (1 << 4) /* Operation queued into CQ */
#define HG_CORE_OP_MULTI_RECV (1 << 5) /* Operation uses multi-recv */

/* Encode type */
#define HG_CORE_TYPE_ENCODE(                                                   \
    subsys, label, ret, buf_ptr, buf_size_left, data, size)                    \
    do {                                                                       \
        HG_CHECK_SUBSYS_ERROR(subsys, buf_size_left < size, label, ret,        \
            HG_OVERFLOW, "Buffer size too small (%" PRIu64 ")",                \
            buf_size_left);                                                    \
        memcpy(buf_ptr, data, size);                                           \
        buf_ptr += size;                                                       \
        buf_size_left -= size;                                                 \
    } while (0)

#define HG_CORE_ENCODE(subsys, label, ret, buf_ptr, buf_size_left, data, type) \
    HG_CORE_TYPE_ENCODE(                                                       \
        subsys, label, ret, buf_ptr, buf_size_left, data, sizeof(type))

/* Decode type */
#define HG_CORE_TYPE_DECODE(                                                   \
    subsys, label, ret, buf_ptr, buf_size_left, data, size)                    \
    do {                                                                       \
        HG_CHECK_SUBSYS_ERROR(subsys, buf_size_left < size, label, ret,        \
            HG_OVERFLOW, "Buffer size too small (%" PRIu64 ")",                \
            buf_size_left);                                                    \
        memcpy(data, buf_ptr, size);                                           \
        buf_ptr += size;                                                       \
        buf_size_left -= size;                                                 \
    } while (0)

#define HG_CORE_DECODE(subsys, label, ret, buf_ptr, buf_size_left, data, type) \
    HG_CORE_TYPE_DECODE(                                                       \
        subsys, label, ret, buf_ptr, buf_size_left, data, sizeof(type))

/* Private accessors */
#define HG_CORE_CONTEXT_CLASS(context)                                         \
    ((struct hg_core_private_class *) (context->core_context.core_class))

#define HG_CORE_HANDLE_CLASS(handle)                                           \
    ((struct hg_core_private_class *) (handle->core_handle.info.core_class))
#define HG_CORE_HANDLE_CONTEXT(handle)                                         \
    ((struct hg_core_private_context *) (handle->core_handle.info.context))

#define HG_CORE_ADDR_CLASS(addr)                                               \
    ((struct hg_core_private_class *) (addr->core_addr.core_class))

/* Name of this subsystem */
#define HG_CORE_SUBSYS_NAME        hg_core
#define HG_CORE_STRINGIFY(x)       HG_UTIL_STRINGIFY(x)
#define HG_CORE_SUBSYS_NAME_STRING HG_CORE_STRINGIFY(HG_CORE_SUBSYS_NAME)

/************************************/
/* Local Type and Struct Definition */
/************************************/

/* Saved init info */
struct hg_core_init_info {
    uint32_t request_post_init;         /* Init request count */
    uint32_t request_post_incr;         /* Increment request count */
    uint32_t multi_recv_op_max;         /* Multi-recv op max */
    uint32_t multi_recv_copy_threshold; /* Copy threshold */
    hg_checksum_level_t checksum_level; /* Checksum level */
    uint8_t progress_mode;              /* Progress mode */
    bool loopback;                      /* Use loopback capability */
    bool na_ext_init;                   /* NA externally initialized */
    bool multi_recv;                    /* Use multi-recv capability */
    bool listen;                        /* Listening on incoming RPC requests */
};

/* RPC map */
struct hg_core_map {
    hg_thread_rwlock_t lock; /* Map RW lock */
    hg_hash_table_t *map;    /* Map */
};

/* More data callbacks */
struct hg_core_more_data_cb {
    hg_return_t (*acquire)(hg_core_handle_t, hg_op_t,
        void (*done_callback)(hg_core_handle_t, hg_return_t)); /* acquire */
    void (*release)(hg_core_handle_t);                         /* release */
};

/* Diag counters */
struct hg_core_counters {
    hg_atomic_int64_t *rpc_req_sent_count;   /* RPC requests sent */
    hg_atomic_int64_t *rpc_req_recv_count;   /* RPC requests received */
    hg_atomic_int64_t *rpc_resp_sent_count;  /* RPC responses sent */
    hg_atomic_int64_t *rpc_resp_recv_count;  /* RPC responses received */
    hg_atomic_int64_t *rpc_req_extra_count;  /* RPC that require extra data */
    hg_atomic_int64_t *rpc_resp_extra_count; /* RPC that require extra data */
    hg_atomic_int64_t *rpc_req_recv_active_count; /* Currently active RPCs */
    hg_atomic_int64_t *rpc_multi_recv_copy_count; /* RPCs requests received that
                                                     required a copy */
    hg_atomic_int64_t *bulk_count;                /* Bulk count */
};

/* HG class */
struct hg_core_private_class {
    struct hg_core_class core_class;    /* Must remain as first field */
    struct hg_core_init_info init_info; /* Saved init info */
#ifdef NA_HAS_SM
    na_sm_id_t host_id; /* Host ID for local identification */
#endif
    struct hg_core_map rpc_map;               /* RPC Map */
    struct hg_core_more_data_cb more_data_cb; /* More data callbacks */
    na_tag_t request_max_tag;                 /* Max value for tag */
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    struct hg_core_counters counters; /* Diag counters */
#endif
    hg_atomic_int32_t n_contexts;  /* Total number of contexts */
    hg_atomic_int32_t n_addrs;     /* Total number of addrs */
    hg_atomic_int32_t n_bulks;     /* Total number of bulk handles */
    hg_atomic_int32_t request_tag; /* Current global RPC tag */
};

/* Poll type */
enum hg_core_poll_type {
    HG_CORE_POLL_LOOPBACK = 1,
#ifdef NA_HAS_SM
    HG_CORE_POLL_SM,
#endif
    HG_CORE_POLL_NA
};

/* Completion queue */
struct hg_core_completion_queue {
    STAILQ_HEAD(, hg_completion_entry) queue; /* Completion queue */
    hg_thread_cond_t cond;                    /* Completion queue cond */
    hg_thread_mutex_t mutex;                  /* Completion queue mutex */
    hg_atomic_int32_t count;                  /* Number of entries */
};

/* List of handles */
struct hg_core_handle_list {
    LIST_HEAD(, hg_core_private_handle) list; /* Handle list */
    hg_thread_spin_t lock;                    /* Handle list lock */
};

/* Handle create callback info */
struct hg_core_handle_create_cb {
    hg_return_t (*callback)(hg_core_handle_t, void *); /* Callback */
    void *arg;                                         /* Callback args */
};

/* Loopback notifications */
struct hg_core_loopback_notify {
    hg_thread_mutex_t mutex;       /* Notify mutex */
    hg_atomic_int32_t must_notify; /* Will notify if set */
    hg_atomic_int32_t nevents;     /* Number of events */
    int event;                     /* Loopback event */
};

/* Multi-recv buffer context */
struct hg_core_multi_recv_op {
    struct hg_core_private_context *context; /* Context */
    void *buf;                               /* Multi-recv buffer */
    size_t buf_size;                         /* Multi-recv buffer size */
    void *plugin_data;                       /* NA plugin data */
    na_op_id_t *op_id;                       /* NA operation ID */
    unsigned int id;                         /* ID for that op */
    hg_atomic_int32_t last;                  /* Buffer is consumed */
    hg_atomic_int32_t ref_count; /* Number of handles using that buffer */
    hg_atomic_int32_t op_count;  /* Total number of ops completed */
};

/* Pool of handles */
struct hg_core_handle_pool {
    hg_thread_mutex_t extend_mutex;          /* To extend pool */
    hg_thread_cond_t extend_cond;            /* To extend pool */
    struct hg_core_private_context *context; /* Context */
    unsigned long flags;                     /* Handle create flags */
    na_class_t *na_class;                    /* NA class */
    na_context_t *na_context;                /* NA context */
    struct hg_core_handle_list pending_list; /* Pending handle list */
    unsigned int count;                      /* Number of handles */
    unsigned int incr_count;                 /* Incremement count */
    bool extending;                          /* When extending the pool */
};

#ifdef HG_HAS_MULTI_PROGRESS
/* Ensure thread safety when progressing context from multiple threads */
struct hg_core_progress_multi {
    hg_thread_cond_t cond;   /* Cond */
    hg_thread_mutex_t mutex; /* Mutex */
    hg_atomic_int32_t count; /* Count */
};
#endif

/* HG context */
struct hg_core_private_context {
    struct hg_core_context core_context; /* Must remain as first field */
#ifdef HG_HAS_MULTI_PROGRESS
    struct hg_core_progress_multi progress_multi; /* Progress multi */
#endif
    struct hg_core_completion_queue backfill_queue; /* Backfill queue */
    struct hg_atomic_queue *completion_queue;       /* Default queue */
    struct hg_core_loopback_notify loopback_notify; /* Loopback notification */
    struct hg_core_handle_list user_list;           /* Created handle list */
    struct hg_core_handle_list internal_list;       /* Created handle list */
    struct hg_core_handle_pool *handle_pool;        /* Pool of handles */
#ifdef NA_HAS_SM
    struct hg_core_handle_pool *sm_handle_pool; /* Pool of SM handles */
#endif
    struct hg_core_multi_recv_op *multi_recv_ops;     /* Multi-recv ops */
    struct hg_core_handle_create_cb handle_create_cb; /* Handle create cb */
    struct hg_bulk_op_pool *hg_bulk_op_pool;          /* Pool of op IDs */
    struct hg_poll_set *poll_set;                     /* Poll set */
    int na_event;                                     /* NA event */
#ifdef NA_HAS_SM
    int na_sm_event; /* NA SM event */
#endif
    hg_atomic_int32_t multi_recv_op_count; /* Number of multi-recv posted */
    hg_atomic_int32_t n_handles;           /* Number of handles */
    hg_atomic_int32_t unposting;           /* Prevent re-posting handles */
    bool posted;                           /* Posted receives on context */
};

/* HG addr */
struct hg_core_private_addr {
    struct hg_core_addr core_addr; /* Must remain as first field */
    size_t na_addr_serialize_size; /* Cached serialization size */
#ifdef NA_HAS_SM
    size_t na_sm_addr_serialize_size; /* Cached serialization size */
    na_sm_id_t host_id;               /* NA SM Host ID */
#endif
    hg_atomic_int32_t ref_count; /* Reference count */
};

/* HG core op type */
#define HG_CORE_OP_TYPE                                                        \
    X(HG_CORE_FORWARD) /*!< Forward completion */                              \
    X(HG_CORE_RESPOND) /*!< Respond completion */                              \
    X(HG_CORE_PROCESS) /*!< Process completion */

#define X(a) a,
enum hg_core_op_type { HG_CORE_OP_TYPE };
#undef X

/* HG core operations */
struct hg_core_ops {
    hg_return_t (*forward)(
        struct hg_core_private_handle *hg_core_handle); /* forward */
    hg_return_t (*respond)(struct hg_core_private_handle *hg_core_handle,
        hg_return_t ret_code); /* respond */
    void (*trigger)(struct hg_core_private_handle *hg_core_handle); /* trigger*/
};

/* HG core handle */
struct hg_core_private_handle {
    struct hg_core_handle core_handle; /* Must remain as first field */
    struct hg_completion_entry hg_completion_entry; /* Completion queue entry */
    LIST_ENTRY(hg_core_private_handle) created;     /* Created list entry */
    LIST_ENTRY(hg_core_private_handle) pending;     /* Pending list entry */
    struct hg_core_header in_header;                /* Input header */
    struct hg_core_header out_header;               /* Output header */
    struct hg_core_handle_list *created_list;       /* Created list */
    na_class_t *na_class;                           /* NA class */
    na_context_t *na_context;                       /* NA context */
    na_addr_t *na_addr;                             /* NA addr */
    hg_core_cb_t request_callback;                  /* Request callback */
    void *request_arg;              /* Request callback arguments */
    hg_core_cb_t response_callback; /* Response callback */
    void *response_arg;             /* Response callback arguments */
    struct hg_core_ops ops;         /* Handle ops */
    void *ack_buf;                  /* Ack buf for more data */
    void *in_buf_plugin_data;       /* Input buffer NA plugin data */
    void *out_buf_plugin_data;      /* Output buffer NA plugin data */
    void *ack_buf_plugin_data;      /* Ack plugin data */
    na_op_id_t *na_send_op_id;      /* Operation ID for send */
    na_op_id_t *na_recv_op_id;      /* Operation ID for recv */
    na_op_id_t *na_ack_op_id;       /* Operation ID for ack */
    struct hg_core_multi_recv_op *multi_recv_op; /* Multi-recv operation */
    void *in_buf_storage;                        /* Storage input buffer */
    size_t in_buf_storage_size;                  /* Storage input buffer size */
    na_tag_t tag;                       /* Tag used for request and response */
    hg_atomic_int32_t ref_count;        /* Reference count */
    hg_atomic_int32_t no_response_done; /* Reference count to reach for done */
    hg_atomic_int32_t status;           /* Handle status */
    hg_atomic_int32_t ret_status;       /* Handle return status */
    hg_atomic_int32_t op_completed_count; /* Completed operation count */
    hg_atomic_int32_t
        op_expected_count;        /* Expected operation count for completion */
    hg_atomic_int32_t flags;      /* Flags */
    enum hg_core_op_type op_type; /* Core operation type */
    hg_return_t ret;              /* Return code associated to handle */
    uint8_t cookie;               /* Cookie */
    bool multi_recv_copy;         /* Copy on multi-recv */
    bool reuse;                   /* Re-use handle once ref_count is 0 */
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    bool active;
#endif
};

/* HG op id */
struct hg_core_op_info_lookup {
    struct hg_core_private_addr *hg_core_addr; /* Address */
};

struct hg_core_op_id {
    struct hg_completion_entry hg_completion_entry; /* Completion queue entry */
    union {
        struct hg_core_op_info_lookup lookup;
    } info;
    struct hg_core_private_context *context; /* Context */
    hg_core_cb_t callback;                   /* Callback */
    void *arg;                               /* Callback arguments */
    hg_cb_type_t type;                       /* Callback type */
};

/********************/
/* Local Prototypes */
/********************/

/**
 * Convert op_type to string.
 */
#ifdef HG_HAS_DEBUG
static const char *
hg_core_op_type_to_string(enum hg_core_op_type op_type);
#endif

/**
 * Init counters.
 */
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
static void
hg_core_counters_init(struct hg_core_counters *hg_core_counters);
#endif

/**
 * Generate a new tag.
 */
static HG_INLINE na_tag_t
hg_core_gen_request_tag(struct hg_core_private_class *hg_core_class);

/**
 * Proc request header and verify it if decoded.
 */
static HG_INLINE hg_return_t
hg_core_proc_header_request(struct hg_core_handle *hg_core_handle,
    struct hg_core_header *hg_core_header, hg_proc_op_t op);

/**
 * Proc response header and verify it if decoded.
 */
static HG_INLINE hg_return_t
hg_core_proc_header_response(struct hg_core_handle *hg_core_handle,
    struct hg_core_header *hg_core_header, hg_proc_op_t op);

/**
 * Initialize class.
 */
static hg_return_t
hg_core_init(const char *na_info_string, bool na_listen, unsigned int version,
    const struct hg_init_info *hg_init_info,
    struct hg_core_private_class **class_p);

/**
 * Finalize class.
 */
static hg_return_t
hg_core_finalize(struct hg_core_private_class *hg_core_class);

/**
 * Get counters.
 */
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
static void
hg_core_class_get_counters(const struct hg_core_counters *counters,
    struct hg_diag_counters *diag_counters);
#endif

/**
 * Create context.
 */
static hg_return_t
hg_core_context_create(struct hg_core_private_class *hg_core_class, uint8_t id,
    struct hg_core_private_context **context_p);

/**
 * Destroy context.
 */
static hg_return_t
hg_core_context_destroy(struct hg_core_private_context *context);

/**
 * Start listening for incoming RPC requests.
 */
static hg_return_t
hg_core_context_post(struct hg_core_private_context *context);

/**
 * Cancel posted requests.
 */
static hg_return_t
hg_core_context_unpost(
    struct hg_core_private_context *context, unsigned int timeout_ms);

/**
 * Allocate multi-recv resources.
 */
static hg_return_t
hg_core_context_multi_recv_alloc(struct hg_core_private_context *context,
    na_class_t *na_class, unsigned int request_count);

/**
 * Free multi-recv resources.
 */
static void
hg_core_context_multi_recv_free(
    struct hg_core_private_context *context, na_class_t *na_class);

/**
 * Post multi-recv operations.
 */
static hg_return_t
hg_core_context_multi_recv_post(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context);

/**
 * Cancel posted multi-recv operations.
 */
static hg_return_t
hg_core_context_multi_recv_unpost(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context);

/**
 * Check list of handles not freed.
 */
static hg_return_t
hg_core_context_check_handle_list(struct hg_core_handle_list *handle_list);

/**
 * Wail until handle lists are empty.
 */
static hg_return_t
hg_core_context_list_wait(struct hg_core_private_context *context,
    struct hg_core_handle_list *handle_list, unsigned int timeout_ms);

/**
 * Create pool of handles.
 */
static hg_return_t
hg_core_handle_pool_create(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context, unsigned long flags,
    unsigned int init_count, unsigned int incr_count,
    struct hg_core_handle_pool **hg_core_handle_pool_p);

/**
 * Destroy pool of handles.
 */
static void
hg_core_handle_pool_destroy(struct hg_core_handle_pool *hg_core_handle_pool);

/**
 * Pool is empty.
 */
static HG_INLINE bool
hg_core_handle_pool_empty(struct hg_core_handle_pool *hg_core_handle_pool);

/**
 * Get handle from pool and extend pool if needed.
 */
static hg_return_t
hg_core_handle_pool_get(struct hg_core_handle_pool *hg_core_handle_pool,
    struct hg_core_private_handle **hg_core_handle_p);

/**
 * Extend pool of handles with incr_count handles.
 */
static hg_return_t
hg_core_handle_pool_extend(struct hg_core_handle_pool *hg_core_handle_pool);

/**
 * Create and insert new handle into pool.
 */
static hg_return_t
hg_core_handle_pool_insert(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context, unsigned long flags,
    struct hg_core_handle_pool *hg_core_handle_pool);

/**
 * Cancel pending operations on pool until pending list is empty.
 */
static hg_return_t
hg_core_handle_pool_unpost(
    struct hg_core_handle_pool *hg_core_handle_pool, unsigned int timeout_ms);

/**
 * Hash map keys based on RPC ID.
 */
static HG_INLINE unsigned int
hg_core_map_hash(hg_hash_table_key_t key);

/**
 * Determine if map keys are equal using RPC ID.
 */
static HG_INLINE int
hg_core_map_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Free value in map.
 */
static void
hg_core_map_value_free(hg_hash_table_value_t value);

/**
 * Lookup entry for RPC ID.
 */
static HG_INLINE struct hg_core_rpc_info *
hg_core_map_lookup(struct hg_core_map *hg_core_map, hg_id_t *id);

/**
 * Insert new entry for RPC ID.
 */
static hg_return_t
hg_core_map_insert(struct hg_core_map *hg_core_map, hg_id_t *id,
    struct hg_core_rpc_info **hg_core_rpc_info_p);

/**
 * Remove entry for RPC ID.
 */
static hg_return_t
hg_core_map_remove(struct hg_core_map *hg_core_map, hg_id_t *id);

/**
 * Lookup addr.
 */
static hg_return_t
hg_core_addr_lookup(struct hg_core_private_class *hg_core_class,
    const char *name, struct hg_core_private_addr **addr_p);

/**
 * Create addr.
 */
static hg_return_t
hg_core_addr_create(struct hg_core_private_class *hg_core_class,
    struct hg_core_private_addr **hg_core_private_addr_p);

/**
 * Free addr.
 */
static void
hg_core_addr_free(struct hg_core_private_addr *hg_core_addr);

/**
 * Free NA addr.
 */
static void
hg_core_addr_free_na(struct hg_core_private_addr *hg_core_addr);

/**
 * Set addr to be removed.
 */
static hg_return_t
hg_core_addr_set_remove(struct hg_core_private_addr *hg_core_addr);

/**
 * Self addr.
 */
static hg_return_t
hg_core_addr_self(struct hg_core_private_class *hg_core_class,
    struct hg_core_private_addr **hg_core_addr_p);

/**
 * Dup addr.
 */
static hg_return_t
hg_core_addr_dup(struct hg_core_private_addr *hg_core_addr,
    struct hg_core_private_addr **hg_core_addr_p);

/**
 * Compare two addresses.
 */
static bool
hg_core_addr_cmp(
    struct hg_core_private_addr *addr1, struct hg_core_private_addr *addr2);

/**
 * Convert addr to string.
 */
static hg_return_t
hg_core_addr_to_string(
    char *buf, hg_size_t *buf_size, struct hg_core_private_addr *hg_core_addr);

/**
 * Get serialize size.
 */
static hg_size_t
hg_core_addr_get_serialize_size(
    struct hg_core_private_addr *hg_core_addr, uint8_t flags);

/**
 * Serialize core address.
 */
static hg_return_t
hg_core_addr_serialize(void *buf, hg_size_t buf_size, uint8_t flags,
    struct hg_core_private_addr *hg_core_addr);

/**
 * Deserialize core address.
 */
static hg_return_t
hg_core_addr_deserialize(struct hg_core_private_class *hg_core_class,
    struct hg_core_private_addr **hg_core_addr_p, const void *buf,
    hg_size_t buf_size);

/**
 * Determine which NA component should be used.
 */
static hg_return_t
hg_core_resolve_na(struct hg_core_private_context *context,
    struct hg_core_private_addr *hg_core_addr, na_class_t **na_class_p,
    na_context_t **na_context_p, na_addr_t **na_addr_p);

/**
 * Create handle.
 */
static hg_return_t
hg_core_create(struct hg_core_private_context *context, na_class_t *na_class,
    na_context_t *na_context, unsigned long flags,
    struct hg_core_private_handle **hg_core_handle_p);

/**
 * Free handle.
 */
static hg_return_t
hg_core_destroy(struct hg_core_private_handle *hg_core_handle);

/**
 * Allocate new handle.
 */
static hg_return_t
hg_core_alloc(struct hg_core_private_context *context, bool user,
    struct hg_core_private_handle **hg_core_handle_p);

/**
 * Free handle.
 */
static void
hg_core_free(struct hg_core_private_handle *hg_core_handle);

/**
 * Allocate NA resources.
 */
static hg_return_t
hg_core_alloc_na(struct hg_core_private_handle *hg_core_handle,
    na_class_t *na_class, na_context_t *na_context, unsigned long flags);

/**
 * Freee NA resources.
 */
static void
hg_core_free_na(struct hg_core_private_handle *hg_core_handle);

/**
 * Reset handle.
 */
static void
hg_core_reset(struct hg_core_private_handle *hg_core_handle);

/**
 * Reset handle and re-post it.
 */
static hg_return_t
hg_core_reset_post(struct hg_core_private_handle *hg_core_handle);

/**
 * Set target addr / RPC ID
 */
static hg_return_t
hg_core_set_rpc(struct hg_core_private_handle *hg_core_handle,
    struct hg_core_private_addr *hg_core_addr, na_addr_t *na_addr, hg_id_t id);

/**
 * Post handle and add it to pending list.
 */
static hg_return_t
hg_core_post(struct hg_core_private_handle *hg_core_handle);

/**
 * Post multi-recv operation.
 */
static hg_return_t
hg_core_post_multi(struct hg_core_multi_recv_op *multi_recv_op,
    na_class_t *na_class, na_context_t *na_context);

/**
 * Release hold on input buffer so that it can be re-used early.
 */
static hg_return_t
hg_core_release_input(struct hg_core_private_handle *hg_core_handle);

/**
 * Forward handle.
 */
static hg_return_t
hg_core_forward(struct hg_core_private_handle *hg_core_handle,
    hg_core_cb_t callback, void *arg, uint8_t flags, hg_size_t payload_size);

/**
 * Forward handle locally.
 */
static hg_return_t
hg_core_forward_self(struct hg_core_private_handle *hg_core_handle);

/**
 * Forward handle through NA.
 */
static hg_return_t
hg_core_forward_na(struct hg_core_private_handle *hg_core_handle);

/**
 * Send response.
 */
static hg_return_t
hg_core_respond(struct hg_core_private_handle *hg_core_handle,
    hg_core_cb_t callback, void *arg, uint8_t flags, hg_size_t payload_size,
    hg_return_t ret_code);

/**
 * Send response locally.
 */
static HG_INLINE hg_return_t
hg_core_respond_self(
    struct hg_core_private_handle *hg_core_handle, hg_return_t ret_code);

/**
 * Do not send response locally.
 */
static HG_INLINE void
hg_core_no_respond_self(struct hg_core_private_handle *hg_core_handle);

/**
 * Send response through NA.
 */
static hg_return_t
hg_core_respond_na(
    struct hg_core_private_handle *hg_core_handle, hg_return_t ret_code);

/**
 * Send input callback.
 */
static HG_INLINE void
hg_core_send_input_cb(const struct na_cb_info *callback_info);

/**
 * Recv input callback.
 */
static void
hg_core_recv_input_cb(const struct na_cb_info *callback_info);

/**
 * Multi-recv input callback.
 */
static void
hg_core_multi_recv_input_cb(const struct na_cb_info *callback_info);

/**
 * Process input.
 */
static hg_return_t
hg_core_process_input(struct hg_core_private_handle *hg_core_handle);

/**
 * Send output callback.
 */
static HG_INLINE void
hg_core_send_output_cb(const struct na_cb_info *callback_info);

/**
 * Recv output callback.
 */
static HG_INLINE void
hg_core_recv_output_cb(const struct na_cb_info *callback_info);

/**
 * Process output.
 */
static hg_return_t
hg_core_process_output(struct hg_core_private_handle *hg_core_handle,
    void (*done_callback)(hg_core_handle_t, hg_return_t));

/**
 * Callback for HG_CORE_MORE_DATA operation.
 */
static HG_INLINE void
hg_core_more_data_complete(hg_core_handle_t handle, hg_return_t ret);

/**
 * Send ack for HG_CORE_MORE_DATA flag on output.
 */
static void
hg_core_send_ack(hg_core_handle_t handle, hg_return_t ret);

/**
 * Ack callback. (HG_CORE_MORE_DATA flag on output)
 */
static HG_INLINE void
hg_core_ack_cb(const struct na_cb_info *callback_info);

/**
 * Process handle.
 */
static hg_return_t
hg_core_process(struct hg_core_private_handle *hg_core_handle);

/**
 * Complete handle operation.
 */
static HG_INLINE void
hg_core_complete_op(struct hg_core_private_handle *hg_core_handle);

/**
 * Complete handle and add to completion queue.
 */
static HG_INLINE void
hg_core_complete(
    struct hg_core_private_handle *hg_core_handle, hg_return_t ret);

/**
 * Completion queue notification callback.
 */
static HG_INLINE hg_return_t
hg_core_loopback_event_set(struct hg_core_private_context *context);

/**
 * Completion queue notification callback.
 */
static HG_INLINE hg_return_t
hg_core_loopback_event_get(
    struct hg_core_private_context *context, bool *notified_p);

/**
 * Get completion entry from queue.
 */
static struct hg_completion_entry *
hg_core_completion_get(struct hg_core_private_context *context);

/**
 * Wait timeout_ms for new completion entry.
 */
static hg_return_t
hg_core_completion_wait(
    struct hg_core_private_context *context, unsigned int timeout_ms);

/**
 * Get current number of completion entries in context's completion queue.
 */
static HG_INLINE unsigned int
hg_core_completion_count(const struct hg_core_private_context *context);

/**
 * Trigger completion entry.
 */
static void
hg_core_completion_trigger(struct hg_completion_entry *hg_completion_entry);

/**
 * Check for events on loopback and if it is safe to wait.
 */
static HG_INLINE bool
hg_core_event_ready_loopback(struct hg_core_private_context *context);

/**
 * Make progress.
 */
static hg_return_t
hg_core_progress_wait(
    struct hg_core_private_context *context, unsigned int timeout_ms);

/**
 * Poll for timeout ms on context.
 */
static hg_return_t
hg_core_poll_wait(struct hg_core_private_context *context,
    unsigned int timeout_ms, bool *progressed_p);

/**
 * Progress context without blocking.
 */
static hg_return_t
hg_core_progress(
    struct hg_core_private_context *context, unsigned int *count_p);

/**
 * Progress context without blocking (legacy path).
 */
static hg_return_t
hg_core_progress_legacy(struct hg_core_private_context *context,
    unsigned int timeout_ms, bool *progressed_p);

/**
 * Progress for timeout ms on on NA layer.
 */
static hg_return_t
hg_core_progress_wait_na(na_class_t *na_class, na_context_t *na_context,
    unsigned int timeout_ms, bool *progressed_p);

/**
 * Progress NA layer without blocking.
 */
static hg_return_t
hg_core_progress_na(
    na_class_t *na_class, na_context_t *na_context, unsigned int *count_p);

/**
 * Trigger callbacks (wait for new callbacks).
 */
static hg_return_t
hg_core_trigger_wait(struct hg_core_private_context *context,
    unsigned int timeout_ms, unsigned int max_count,
    unsigned int *actual_count_p);

/**
 * Trigger available callbacks.
 */
static void
hg_core_trigger(struct hg_core_private_context *context, unsigned int max_count,
    unsigned int *actual_count_p);

/**
 * Trigger callback from HG lookup op ID.
 */
static HG_INLINE void
hg_core_trigger_lookup_entry(struct hg_core_op_id *hg_core_op_id);

/**
 * Trigger callback from HG core handle.
 */
static HG_INLINE void
hg_core_trigger_entry(struct hg_core_private_handle *hg_core_handle);

/**
 * Trigger callback from self HG core handle.
 */
static HG_INLINE void
hg_core_trigger_self(struct hg_core_private_handle *hg_core_handle);

/**
 * Trigger callback from HG core handle.
 */
static HG_INLINE void
hg_core_trigger_na(struct hg_core_private_handle *hg_core_handle);

/**
 * Trigger RPC handler callback from HG core handle.
 */
static void
hg_core_trigger_process(struct hg_core_private_handle *hg_core_handle);

/**
 * Trigger forward callback.
 */
static HG_INLINE void
hg_core_trigger_forward_cb(struct hg_core_private_handle *hg_core_handle);

/**
 * Trigger respond callback.
 */
static HG_INLINE void
hg_core_trigger_respond_cb(struct hg_core_private_handle *hg_core_handle);

/**
 * Wrapper for local callback execution.
 */
static void
hg_core_trigger_self_respond_cb(struct hg_core_private_handle *hg_core_handle);

/**
 * Cancel handle.
 */
static hg_return_t
hg_core_cancel(struct hg_core_private_handle *hg_core_handle);

/*******************/
/* Local Variables */
/*******************/

/* Callback type string table */
#define X(a) #a,
#ifdef HG_HAS_DEBUG
static const char *const hg_core_op_type_name_g[] = {HG_CORE_OP_TYPE};
#endif
#undef X

/* Default ops */
static const struct hg_core_ops hg_core_ops_na_g = {
    .forward = hg_core_forward_na,
    .respond = hg_core_respond_na,
    .trigger = hg_core_trigger_na};

static const struct hg_core_ops hg_core_ops_self_g = {
    .forward = hg_core_forward_self,
    .respond = hg_core_respond_self,
    .trigger = hg_core_trigger_self};

/*---------------------------------------------------------------------------*/
#ifdef HG_HAS_DEBUG
static const char *
hg_core_op_type_to_string(enum hg_core_op_type op_type)
{
    return hg_core_op_type_name_g[op_type];
}
#endif

/*---------------------------------------------------------------------------*/
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
static void
hg_core_counters_init(struct hg_core_counters *hg_core_counters)
{
    /* TODO we could revert the linked list to avoid registration in reverse
     * order */
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->bulk_count, "bulk_count",
        "Bulk transfers (inc. extra bulks)");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_multi_recv_copy_count,
        "rpc_multi_recv_copy_count", "RPC requests received requiring a copy");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_req_recv_active_count,
        "rpc_req_recv_active_count", "RPC requests received still active");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_resp_extra_count,
        "rpc_resp_extra_count", "RPCs with extra bulk response");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_req_extra_count,
        "rpc_req_extra_count", "RPCs with extra bulk request");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_resp_recv_count,
        "rpc_resp_recv_count", "RPC responses received");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_resp_sent_count,
        "rpc_resp_sent_count", "RPC responses sent");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_req_recv_count,
        "rpc_req_recv_count", "RPC requests received");
    HG_LOG_ADD_COUNTER64(hg_diag, &hg_core_counters->rpc_req_sent_count,
        "rpc_req_sent_count", "RPC requests sent");
}
#endif

/*---------------------------------------------------------------------------*/
static HG_INLINE na_tag_t
hg_core_gen_request_tag(struct hg_core_private_class *hg_core_class)
{
    na_tag_t request_tag = 0;

    /* Compare and swap tag if reached max tag */
    if (!hg_atomic_cas32(&hg_core_class->request_tag,
            (int32_t) hg_core_class->request_max_tag, 0)) {
        /* Increment tag */
        request_tag = (na_tag_t) hg_atomic_incr32(&hg_core_class->request_tag);
    }

    return request_tag;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
hg_core_proc_header_request(struct hg_core_handle *hg_core_handle,
    struct hg_core_header *hg_core_header, hg_proc_op_t op)
{
    char *header_buf =
        (char *) hg_core_handle->in_buf + hg_core_handle->na_in_header_offset;
    size_t header_buf_size =
        hg_core_handle->in_buf_size - hg_core_handle->na_in_header_offset;
    hg_return_t ret;

    /* Proc request header */
    ret = hg_core_header_request_proc(
        op, header_buf, header_buf_size, hg_core_header);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not process request header");

    if (op == HG_DECODE) {
        ret = hg_core_header_request_verify(hg_core_header);
        HG_CHECK_SUBSYS_HG_ERROR(
            rpc, error, ret, "Could not verify request header");
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
hg_core_proc_header_response(struct hg_core_handle *hg_core_handle,
    struct hg_core_header *hg_core_header, hg_proc_op_t op)
{
    char *header_buf =
        (char *) hg_core_handle->out_buf + hg_core_handle->na_out_header_offset;
    size_t header_buf_size =
        hg_core_handle->out_buf_size - hg_core_handle->na_out_header_offset;
    hg_return_t ret;

    /* Proc response header */
    ret = hg_core_header_response_proc(
        op, header_buf, header_buf_size, hg_core_header);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not process response header");

    if (op == HG_DECODE) {
        ret = hg_core_header_response_verify(hg_core_header);
        HG_CHECK_SUBSYS_HG_ERROR(
            rpc, error, ret, "Could not verify response header");
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_init(const char *na_info_string, bool na_listen, unsigned int version,
    const struct hg_init_info *hg_init_info_p,
    struct hg_core_private_class **class_p)
{
    struct hg_init_info hg_init_info = HG_INIT_INFO_INITIALIZER;
    struct na_init_info na_init_info = NA_INIT_INFO_INITIALIZER;
    unsigned int na_version = NA_VERSION(NA_VERSION_MAJOR, NA_VERSION_MINOR);
    struct na_init_info *na_init_info_p = NULL;
    struct hg_core_private_class *hg_core_class = NULL;
#ifdef NA_HAS_SM
    const char *na_class_name;
#endif
    hg_return_t ret;
    int rc;

    /* Prevent newer versions */
    HG_CHECK_SUBSYS_ERROR(cls,
        HG_VERSION_LT(HG_VERSION(HG_VERSION_MAJOR, HG_VERSION_MINOR), version),
        error, ret, HG_INVALID_ARG, "API version cannot be newer than library");

    /* Create new HG class */
    hg_core_class =
        (struct hg_core_private_class *) calloc(1, sizeof(*hg_core_class));
    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error_free, ret, HG_NOMEM,
        "Could not allocate HG class");

    hg_atomic_init32(&hg_core_class->request_tag, 0);
    hg_atomic_init32(&hg_core_class->n_contexts, 0);
    hg_atomic_init32(&hg_core_class->n_addrs, 0);
    hg_atomic_init32(&hg_core_class->n_bulks, 0);

    /* Initialize mutex */
    rc = hg_thread_rwlock_init(&hg_core_class->rpc_map.lock);
    HG_CHECK_SUBSYS_ERROR(cls, rc != HG_UTIL_SUCCESS, error_free, ret, HG_NOMEM,
        "hg_thread_rwlock_init() failed");

    /* Create new function map */
    hg_core_class->rpc_map.map =
        hg_hash_table_new(hg_core_map_hash, hg_core_map_equal);
    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class->rpc_map.map == NULL, error, ret,
        HG_NOMEM, "Could not create RPC map");

    /* Automatically free all the values with the hash map */
    hg_hash_table_register_free_functions(
        hg_core_class->rpc_map.map, NULL, hg_core_map_value_free);

    /* Ensure init info is API compatible */
    if (hg_init_info_p) {
        HG_CHECK_SUBSYS_ERROR(cls, version == 0, error, ret, HG_INVALID_ARG,
            "API version cannot be 0");
        HG_LOG_SUBSYS_DEBUG(cls, "HG init info version used: v%d.%d",
            HG_MAJOR(version), HG_MINOR(version));
        na_init_info_p = &na_init_info;
        na_init_info_dup_4_0(&na_init_info, &hg_init_info_p->na_init_info);

        /* Get init info and overwrite defaults */
        if (HG_VERSION_GE(version, HG_VERSION(2, 4))) {
            hg_init_info = *hg_init_info_p;
            /* Duplicate traffic class field for now, this will be fixed in
             * a later major version. */
            na_init_info.traffic_class = hg_init_info.traffic_class;
        } else if (HG_VERSION_GE(version, HG_VERSION(2, 3)))
            hg_init_info_dup_2_3(&hg_init_info,
                (const struct hg_init_info_2_3 *) hg_init_info_p);
        else
            hg_init_info_dup_2_2(&hg_init_info,
                (const struct hg_init_info_2_2 *) hg_init_info_p);

        HG_LOG_SUBSYS_DEBUG(cls,
            "HG Init info: na_class=%p, request_post_init=%" PRIu32
            ", request_post_incr=%" PRId32 ", auto_sm=%" PRIu8
            ", sm_info_string=%s, checksum_level=%d, no_bulk_eager=%" PRIu8
            ", no_loopback=%" PRIu8 ", stats=%" PRIu8 ", no_multi_recv=%" PRIu8
            ", release_input_early=%" PRIu8
            ", traffic_class=%d, no_overflow=%d, multi_recv_op_max=%u, "
            "multi_recv_copy_threshold=%u",
            (void *) hg_init_info.na_class, hg_init_info.request_post_init,
            hg_init_info.request_post_incr, hg_init_info.auto_sm,
            hg_init_info.sm_info_string, hg_init_info.checksum_level,
            hg_init_info.no_bulk_eager, hg_init_info.no_loopback,
            hg_init_info.stats, hg_init_info.no_multi_recv,
            hg_init_info.release_input_early, hg_init_info.traffic_class,
            hg_init_info.no_overflow, hg_init_info.multi_recv_op_max,
            hg_init_info.multi_recv_copy_threshold);
    }

    /* Set post init / incr / multi-recv values  */
    hg_core_class->init_info.request_post_init =
        (hg_init_info.request_post_init == 0) ? HG_CORE_POST_INIT
                                              : hg_init_info.request_post_init;
    if (hg_init_info.request_post_incr < 0)
        hg_core_class->init_info.request_post_incr = 0;
    else if (hg_init_info.request_post_incr == 0)
        hg_core_class->init_info.request_post_incr = HG_CORE_POST_INCR;
    else
        hg_core_class->init_info.request_post_incr =
            (uint32_t) hg_init_info.request_post_incr;
    hg_core_class->init_info.multi_recv_op_max =
        (hg_init_info.multi_recv_op_max == 0) ? HG_CORE_MULTI_RECV_OP_COUNT
                                              : hg_init_info.multi_recv_op_max;

    HG_CHECK_SUBSYS_ERROR(cls,
        hg_init_info.multi_recv_copy_threshold >
            (unsigned int) hg_core_class->init_info.multi_recv_op_max,
        error, ret, HG_INVALID_ARG,
        "multi_recv_copy_threshold (%u) cannot exceed multi_recv_op_max "
        "(%u)",
        hg_init_info.multi_recv_copy_threshold,
        (unsigned int) hg_core_class->init_info.multi_recv_op_max);
    hg_core_class->init_info.multi_recv_copy_threshold =
        hg_init_info.multi_recv_copy_threshold;

#ifdef HG_HAS_CHECKSUMS
    /* Save checksum level */
    hg_core_class->init_info.checksum_level = hg_init_info.checksum_level;
#else
    HG_CHECK_SUBSYS_WARNING(cls,
        hg_init_info.checksum_level != HG_CHECKSUM_NONE,
        "Option checksum_level requires CMake option MERCURY_USE_CHECKSUMS "
        "to be turned ON.");
    hg_core_class->init_info.checksum_level = HG_CHECKSUM_NONE;
#endif

    /* Save progress mode */
    hg_core_class->init_info.progress_mode = na_init_info.progress_mode;

    /* Loopback capability */
    hg_core_class->init_info.loopback = !hg_init_info.no_loopback;

    /* Listening */
    hg_core_class->init_info.listen = na_listen;

    /* Stats / counters */
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    hg_core_counters_init(&hg_core_class->counters);
#endif

    if (hg_init_info.stats) {
#ifdef HG_HAS_DEBUG
        hg_log_set_subsys_level("diag", HG_LOG_LEVEL_DEBUG);
#else
        HG_LOG_SUBSYS_WARNING(cls, "stats option requires MERCURY_ENABLE_DEBUG "
                                   "CMake option to be turned ON.");
#endif
    }

    if (hg_init_info.na_class != NULL) {
        /* External NA class */
        hg_core_class->core_class.na_class = hg_init_info.na_class;
        hg_core_class->init_info.na_ext_init = true;
    } else {
        /* Initialize NA if not provided externally */
        hg_core_class->core_class.na_class = NA_Initialize_opt2(na_info_string,
            hg_core_class->init_info.listen, na_version, na_init_info_p);
        HG_CHECK_SUBSYS_ERROR(cls, hg_core_class->core_class.na_class == NULL,
            error, ret, HG_NA_ERROR,
            "Could not initialize NA class (info_string=%s, listen=%d)",
            na_info_string, hg_core_class->init_info.listen);
    }

    /* Multi-recv capability */
    hg_core_class->init_info.multi_recv =
        NA_Has_opt_feature(
            hg_core_class->core_class.na_class, NA_OPT_MULTI_RECV) &&
        !hg_init_info.no_multi_recv;
    HG_LOG_SUBSYS_DEBUG(
        cls, "Multi-recv set to %" PRIu8, hg_core_class->init_info.multi_recv);
    if (hg_core_class->init_info.multi_recv &&
        (hg_core_class->init_info.request_post_incr == 0)) {
        hg_core_class->init_info.request_post_incr = HG_CORE_POST_INCR;
        HG_LOG_SUBSYS_WARNING(cls,
            "Using multi-recv with no handle post increment is currently not "
            "supported, resetting to default value of %d",
            HG_CORE_POST_INCR);
    }

    /* Compute max request tag */
    hg_core_class->request_max_tag =
        NA_Msg_get_max_tag(hg_core_class->core_class.na_class);
    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class->request_max_tag == 0, error, ret,
        HG_NA_ERROR, "NA Max tag is not defined");

#ifdef NA_HAS_SM
    /* Retrieve NA class name */
    na_class_name = NA_Get_class_name(hg_core_class->core_class.na_class);

    /* Initialize SM plugin */
    if (hg_init_info.auto_sm && ((strcmp(na_class_name, "mpi") == 0) ||
                                    (strcmp(na_class_name, "na") == 0))) {
        HG_LOG_SUBSYS_WARNING(cls,
            "Auto SM mode is not compatible with current NA class, disabling");
    } else if (hg_init_info.auto_sm) {
        char info_string[HG_CORE_ADDR_MAX_SIZE], *info_string_p;
        na_tag_t na_sm_max_tag;
        na_return_t na_ret;

        if (hg_init_info.sm_info_string != NULL) {
            rc = snprintf(info_string, HG_CORE_ADDR_MAX_SIZE, "na+sm://%s",
                hg_init_info.sm_info_string);
            HG_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > HG_CORE_ADDR_MAX_SIZE,
                error, ret, HG_OVERFLOW, "snprintf() failed, rc: %d", rc);
            info_string_p = info_string;
        } else
            info_string_p = "na+sm";

        /* Initialize NA SM first so that tmp directories are created */
        hg_core_class->core_class.na_sm_class =
            NA_Initialize_opt2(info_string_p, hg_core_class->init_info.listen,
                na_version, na_init_info_p);
        HG_CHECK_SUBSYS_ERROR(cls,
            hg_core_class->core_class.na_sm_class == NULL, error, ret,
            HG_NA_ERROR,
            "Could not initialize NA SM class (info_string=%s, listen=%d)",
            info_string_p, hg_core_class->init_info.listen);

        /* Get SM host ID */
        na_ret = NA_SM_Host_id_get(&hg_core_class->host_id);
        HG_CHECK_SUBSYS_ERROR(cls, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_SM_Host_id_get() failed (%s)",
            NA_Error_to_string(na_ret));

        /* Get max tag */
        na_sm_max_tag =
            NA_Msg_get_max_tag(hg_core_class->core_class.na_sm_class);
        HG_CHECK_SUBSYS_ERROR(cls, na_sm_max_tag == 0, error, ret, HG_NA_ERROR,
            "NA Max tag is not defined");
        hg_core_class->request_max_tag =
            MIN(hg_core_class->request_max_tag, na_sm_max_tag);
    }
#else
    HG_CHECK_SUBSYS_WARNING(cls, hg_init_info.auto_sm,
        "Option auto_sm requested but NA SM pluging was not compiled, "
        "please turn ON NA_USE_SM in CMake options");
#endif

    *class_p = hg_core_class;

    return HG_SUCCESS;

error:
    if (hg_core_class->core_class.na_class != NULL &&
        !hg_core_class->init_info.na_ext_init) {
        na_return_t na_ret = NA_Finalize(hg_core_class->core_class.na_class);
        HG_CHECK_SUBSYS_ERROR_DONE(cls, na_ret != NA_SUCCESS,
            "Could not finalize NA class (%s)", NA_Error_to_string(na_ret));
    }
#ifdef NA_HAS_SM
    if (hg_core_class->core_class.na_sm_class != NULL) {
        na_return_t na_ret = NA_Finalize(hg_core_class->core_class.na_sm_class);
        HG_CHECK_SUBSYS_ERROR_DONE(cls, na_ret != NA_SUCCESS,
            "Could not finalize NA SM class (%s)", NA_Error_to_string(na_ret));
    }
#endif
    if (hg_core_class->rpc_map.map)
        hg_hash_table_free(hg_core_class->rpc_map.map);
    (void) hg_thread_rwlock_destroy(&hg_core_class->rpc_map.lock);

error_free:
    free(hg_core_class);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_finalize(struct hg_core_private_class *hg_core_class)
{
    int32_t n_addrs, n_contexts, n_bulks;
    hg_return_t ret;

    if (hg_core_class == NULL)
        return HG_SUCCESS;

    n_bulks = hg_atomic_get32(&hg_core_class->n_bulks);
    HG_CHECK_SUBSYS_ERROR(cls, n_bulks != 0, error, ret, HG_BUSY,
        "HG bulk handles must be destroyed before finalizing HG (%d "
        "remaining)",
        n_bulks);

    n_contexts = hg_atomic_get32(&hg_core_class->n_contexts);
    HG_CHECK_SUBSYS_ERROR(cls, n_contexts != 0, error, ret, HG_BUSY,
        "HG contexts must be destroyed before finalizing HG (%d remaining)",
        n_contexts);

    n_addrs = hg_atomic_get32(&hg_core_class->n_addrs);
    HG_CHECK_SUBSYS_ERROR(cls, n_addrs != 0, error, ret, HG_BUSY,
        "HG addrs must be freed before finalizing HG (%d remaining)", n_addrs);

    /* Finalize NA class */
    if (hg_core_class->core_class.na_class != NULL &&
        !hg_core_class->init_info.na_ext_init) {
        na_return_t na_ret = NA_Finalize(hg_core_class->core_class.na_class);
        HG_CHECK_SUBSYS_ERROR(cls, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not finalize NA class (%s)",
            NA_Error_to_string(na_ret));
        hg_core_class->core_class.na_class = NULL;
    }

#ifdef NA_HAS_SM
    /* Finalize NA SM class */
    if (hg_core_class->core_class.na_sm_class != NULL) {
        na_return_t na_ret = NA_Finalize(hg_core_class->core_class.na_sm_class);
        HG_CHECK_SUBSYS_ERROR(cls, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not finalize NA SM class (%s)",
            NA_Error_to_string(na_ret));
        hg_core_class->core_class.na_sm_class = NULL;
    }
#endif

    /* Free user data */
    if (hg_core_class->core_class.data_free_callback)
        hg_core_class->core_class.data_free_callback(
            hg_core_class->core_class.data);

    /* Delete RPC map */
    if (hg_core_class->rpc_map.map != NULL) {
        hg_hash_table_free(hg_core_class->rpc_map.map);
        hg_core_class->rpc_map.map = NULL;
    }
    (void) hg_thread_rwlock_destroy(&hg_core_class->rpc_map.lock);
    free(hg_core_class);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
static void
hg_core_class_get_counters(const struct hg_core_counters *counters,
    struct hg_diag_counters *diag_counters)
{
    *diag_counters = (struct hg_diag_counters){
        .rpc_req_sent_count =
            (uint64_t) hg_atomic_get64(counters->rpc_req_sent_count),
        .rpc_req_recv_count =
            (uint64_t) hg_atomic_get64(counters->rpc_req_recv_count),
        .rpc_resp_sent_count =
            (uint64_t) hg_atomic_get64(counters->rpc_resp_sent_count),
        .rpc_resp_recv_count =
            (uint64_t) hg_atomic_get64(counters->rpc_resp_recv_count),
        .rpc_req_extra_count =
            (uint64_t) hg_atomic_get64(counters->rpc_req_extra_count),
        .rpc_resp_extra_count =
            (uint64_t) hg_atomic_get64(counters->rpc_resp_extra_count),
        .rpc_req_recv_active_count =
            (uint64_t) hg_atomic_get64(counters->rpc_req_recv_active_count),
        .rpc_multi_recv_copy_count =
            (uint64_t) hg_atomic_get64(counters->rpc_multi_recv_copy_count),
        .bulk_count = (uint64_t) hg_atomic_get64(counters->bulk_count)};
}
#endif

/*---------------------------------------------------------------------------*/
void
hg_core_bulk_incr(hg_core_class_t *hg_core_class)
{
    hg_atomic_incr32(
        &((struct hg_core_private_class *) hg_core_class)->n_bulks);
}

/*---------------------------------------------------------------------------*/
void
hg_core_bulk_decr(hg_core_class_t *hg_core_class)
{
    hg_atomic_decr32(
        &((struct hg_core_private_class *) hg_core_class)->n_bulks);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_create(struct hg_core_private_class *hg_core_class, uint8_t id,
    struct hg_core_private_context **context_p)
{
    struct hg_core_private_context *context = NULL;
    struct hg_core_completion_queue *backfill_queue = NULL;
    hg_return_t ret;
    int na_poll_fd, loopback_event = 0, rc;
    bool backfill_queue_mutex_init = false, backfill_queue_cond_init = false,
         loopback_notify_mutex_init = false, user_list_lock_init = false,
         internal_list_lock_init = false;
#ifdef HG_HAS_MULTI_PROGRESS
    struct hg_core_progress_multi *progress_multi = NULL;
    bool progress_multi_mutex_init = false, progress_multi_cond_init = false;
#endif

    context = (struct hg_core_private_context *) calloc(1, sizeof(*context));
    HG_CHECK_SUBSYS_ERROR(ctx, context == NULL, error, ret, HG_NOMEM,
        "Could not allocate HG context");
    hg_atomic_init32(&context->n_handles, 0);
    hg_atomic_init32(&context->unposting, 0);

    context->core_context.core_class = (struct hg_core_class *) hg_core_class;
    backfill_queue = &context->backfill_queue;

    STAILQ_INIT(&backfill_queue->queue);
    hg_atomic_init32(&backfill_queue->count, 0);
    rc = hg_thread_mutex_init(&backfill_queue->mutex);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_mutex_init() failed");
    backfill_queue_mutex_init = true;
    rc = hg_thread_cond_init(&backfill_queue->cond);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_cond_init() failed");
    backfill_queue_cond_init = true;

    context->completion_queue =
        hg_atomic_queue_alloc(HG_CORE_ATOMIC_QUEUE_SIZE);
    HG_CHECK_SUBSYS_ERROR(ctx, context->completion_queue == NULL, error, ret,
        HG_NOMEM, "Could not allocate queue");

    /* Notifications of completion queue events */
    hg_atomic_init32(&context->loopback_notify.must_notify, 0);
    hg_atomic_init32(&context->loopback_notify.nevents, 0);
    rc = hg_thread_mutex_init(&context->loopback_notify.mutex);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_mutex_init() failed");
    loopback_notify_mutex_init = true;

    LIST_INIT(&context->user_list.list);
    rc = hg_thread_spin_init(&context->user_list.lock);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_spin_init() failed");
    user_list_lock_init = true;

    LIST_INIT(&context->internal_list.list);
    rc = hg_thread_spin_init(&context->internal_list.lock);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_spin_init() failed");
    internal_list_lock_init = true;

#ifdef HG_HAS_MULTI_PROGRESS
    /* Initialize multi-progress lock */
    progress_multi = &context->progress_multi;
    hg_atomic_init32(&progress_multi->count, 0);
    rc = hg_thread_mutex_init(&progress_multi->mutex);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_mutex_init() failed");
    progress_multi_mutex_init = true;

    rc = hg_thread_cond_init(&progress_multi->cond);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_cond_init() failed");
    progress_multi_cond_init = true;
#endif

    /* Create NA context */
    context->core_context.na_context =
        NA_Context_create_id(hg_core_class->core_class.na_class, id);
    HG_CHECK_SUBSYS_ERROR(ctx, context->core_context.na_context == NULL, error,
        ret, HG_NOMEM, "Could not create NA context");

#ifdef NA_HAS_SM
    if (hg_core_class->core_class.na_sm_class) {
        context->core_context.na_sm_context =
            NA_Context_create(hg_core_class->core_class.na_sm_class);
        HG_CHECK_SUBSYS_ERROR(ctx, context->core_context.na_sm_context == NULL,
            error, ret, HG_NOMEM, "Could not create NA SM context");
    }
#endif

    /* If NA plugin exposes fd, we will use poll set and use appropriate
     * progress function */
    na_poll_fd = NA_Poll_get_fd(
        hg_core_class->core_class.na_class, context->core_context.na_context);

    if (!(hg_core_class->init_info.progress_mode & NA_NO_BLOCK) &&
        (na_poll_fd > 0)) {
        struct hg_poll_event event = {.events = HG_POLLIN, .data.u64 = 0};

        /* Create poll set */
        context->poll_set = hg_poll_create();
        HG_CHECK_SUBSYS_ERROR(ctx, context->poll_set == NULL, error, ret,
            HG_NOMEM, "Could not create poll set");

        event.data.u32 = (uint32_t) HG_CORE_POLL_NA;
        rc = hg_poll_add(context->poll_set, na_poll_fd, &event);
        HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
            "hg_poll_add() failed (na_poll_fd=%d)", na_poll_fd);
        context->na_event = na_poll_fd;

#ifdef NA_HAS_SM
        if (hg_core_class->core_class.na_sm_class &&
            context->core_context.na_sm_context) {
            na_poll_fd = NA_Poll_get_fd(hg_core_class->core_class.na_sm_class,
                context->core_context.na_sm_context);
            HG_CHECK_SUBSYS_ERROR(ctx, na_poll_fd < 0, error, ret,
                HG_PROTOCOL_ERROR, "Could not get NA SM poll fd");

            event.data.u32 = (uint32_t) HG_CORE_POLL_SM;
            rc = hg_poll_add(context->poll_set, na_poll_fd, &event);
            HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
                HG_NOMEM, "hg_poll_add() failed (na_poll_fd=%d)", na_poll_fd);
            context->na_sm_event = na_poll_fd;
        }
#endif

        if (hg_core_class->init_info.loopback) {
            /* Create event for completion queue notification */
            loopback_event = hg_event_create();
            HG_CHECK_SUBSYS_ERROR(ctx, loopback_event < 0, error, ret, HG_NOMEM,
                "Could not create event");

            /* Add event to context poll set */
            event.data.u32 = (uint32_t) HG_CORE_POLL_LOOPBACK;
            rc = hg_poll_add(context->poll_set, loopback_event, &event);
            HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
                HG_NOMEM, "hg_poll_add() failed (loopback_event=%d)",
                loopback_event);
            context->loopback_notify.event = loopback_event;
        }
    }

    /* Assign context ID */
    context->core_context.id = id;

    /* Create pool of bulk op IDs */
    ret = hg_bulk_op_pool_create((hg_core_context_t *) context,
        HG_CORE_BULK_OP_INIT_COUNT, &context->hg_bulk_op_pool);
    HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not create bulk op pool");

    /* Increment context count of parent class */
    hg_atomic_incr32(&HG_CORE_CONTEXT_CLASS(context)->n_contexts);

    *context_p = context;

    return HG_SUCCESS;

error:
    if (context != NULL) {
        if (context->poll_set != NULL) {
            if (context->na_event > 0) {
                rc = hg_poll_remove(context->poll_set, context->na_event);
                HG_CHECK_SUBSYS_ERROR_DONE(ctx, rc != HG_UTIL_SUCCESS,
                    "Could not remove NA poll descriptor from poll set");
            }
#ifdef NA_HAS_SM
            if (context->na_sm_event > 0) {
                rc = hg_poll_remove(context->poll_set, context->na_sm_event);
                HG_CHECK_SUBSYS_ERROR_DONE(ctx, rc != HG_UTIL_SUCCESS,
                    "Could not remove NA SM poll descriptor from poll set");
            }
#endif
            if (context->loopback_notify.event > 0) {
                rc = hg_poll_remove(
                    context->poll_set, context->loopback_notify.event);
                HG_CHECK_SUBSYS_ERROR_DONE(ctx, rc != HG_UTIL_SUCCESS,
                    "Could not remove loopback poll descriptor from poll set");
            }
            rc = hg_poll_destroy(context->poll_set);
            HG_CHECK_SUBSYS_ERROR_DONE(
                ctx, rc != HG_UTIL_SUCCESS, "Could not destroy poll set");
        }

        if (loopback_event > 0) {
            rc = hg_event_destroy(loopback_event);
            HG_CHECK_SUBSYS_ERROR_DONE(
                ctx, rc != HG_UTIL_SUCCESS, "Could not destroy loopback event");
        }

        if (context->core_context.na_context != NULL) {
            na_return_t na_ret =
                NA_Context_destroy(hg_core_class->core_class.na_class,
                    context->core_context.na_context);
            HG_CHECK_SUBSYS_ERROR_DONE(ctx, na_ret != NA_SUCCESS,
                "Could not destroy NA context (%s)",
                NA_Error_to_string(na_ret));
        }
#ifdef NA_HAS_SM
        if (context->core_context.na_sm_context != NULL) {
            na_return_t na_ret =
                NA_Context_destroy(hg_core_class->core_class.na_sm_class,
                    context->core_context.na_sm_context);
            HG_CHECK_SUBSYS_ERROR_DONE(ctx, na_ret != NA_SUCCESS,
                "Could not destroy NA SM context (%s)",
                NA_Error_to_string(na_ret));
        }
#endif

        if (backfill_queue_mutex_init)
            (void) hg_thread_mutex_destroy(&backfill_queue->mutex);
        if (backfill_queue_cond_init)
            (void) hg_thread_cond_destroy(&backfill_queue->cond);
        if (loopback_notify_mutex_init)
            (void) hg_thread_mutex_destroy(&context->loopback_notify.mutex);
        if (user_list_lock_init)
            (void) hg_thread_spin_destroy(&context->user_list.lock);
        if (internal_list_lock_init)
            (void) hg_thread_spin_destroy(&context->internal_list.lock);
#ifdef HG_HAS_MULTI_PROGRESS
        if (progress_multi_mutex_init)
            (void) hg_thread_mutex_destroy(&progress_multi->mutex);
        if (progress_multi_cond_init)
            (void) hg_thread_cond_destroy(&progress_multi->cond);
#endif
        hg_atomic_queue_free(context->completion_queue);
        free(context);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_destroy(struct hg_core_private_context *context)
{
    struct hg_core_private_class *hg_core_class = NULL;
#ifdef HG_HAS_MULTI_PROGRESS
    struct hg_core_progress_multi *progress_multi = NULL;
#endif
    struct hg_core_completion_queue *backfill_queue = NULL;
    bool empty;
    hg_return_t ret;
    int rc;

    if (context == NULL)
        return HG_SUCCESS;

    /* Keep reference to class */
    hg_core_class = HG_CORE_CONTEXT_CLASS(context);
#ifdef HG_HAS_MULTI_PROGRESS
    /* Check that we are no longer progressing */
    progress_multi = &context->progress_multi;
    HG_CHECK_SUBSYS_ERROR(ctx, hg_atomic_get32(&progress_multi->count) > 0,
        error, ret, HG_BUSY, "Still progressing on context");
#endif

    if (context->posted) {
        /* Unpost requests */
        ret = hg_core_context_unpost(context, HG_CORE_CLEANUP_TIMEOUT);
        HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not unpost requests");
    }

    /* Wait on created list (user created handles) */
    ret = hg_core_context_list_wait(
        context, &context->user_list, HG_CORE_CLEANUP_TIMEOUT);
    HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not wait on handle list");

    /* Number of handles for that context should be 0 */
    if (hg_atomic_get32(&context->n_handles) > 0) {
        HG_LOG_SUBSYS_ERROR(ctx,
            "HG core handles must be freed before destroying context (%" PRId32
            " remaining)",
            hg_atomic_get32(&context->n_handles));

        ret = hg_core_context_check_handle_list(&context->user_list);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "User-created handles are still in use");

        ret = hg_core_context_check_handle_list(&context->internal_list);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Internal handles are still in use");
    }

    /* Check that backfill completion queue is empty now */
    backfill_queue = &context->backfill_queue;
    hg_thread_mutex_lock(&backfill_queue->mutex);
    empty = STAILQ_EMPTY(&backfill_queue->queue);
    hg_thread_mutex_unlock(&backfill_queue->mutex);
    HG_CHECK_SUBSYS_ERROR(ctx, empty == false, error, ret, HG_BUSY,
        "Completion queue should be empty");

    /* Check that atomic completion queue is empty now */
    empty = hg_atomic_queue_is_empty(context->completion_queue);
    HG_CHECK_SUBSYS_ERROR(ctx, empty == false, error, ret, HG_BUSY,
        "Completion queue should be empty");

    /* Destroy pool of bulk op IDs */
    if (context->hg_bulk_op_pool != NULL) {
        hg_bulk_op_pool_destroy(context->hg_bulk_op_pool);
        context->hg_bulk_op_pool = NULL;
    }

    /* Stop listening for events */
    if (context->loopback_notify.event > 0) {
        rc = hg_poll_remove(context->poll_set, context->loopback_notify.event);
        HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
            HG_NOENTRY, "Could not remove loopback notify event from poll set");

        rc = hg_event_destroy(context->loopback_notify.event);
        HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
            HG_NOENTRY, "Could not destroy loopback notify event");
        context->loopback_notify.event = 0;
    }

    if (context->na_event > 0) {
        rc = hg_poll_remove(context->poll_set, context->na_event);
        HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
            HG_NOENTRY, "Could not remove NA event from poll set");
        context->na_event = 0;
    }

#ifdef NA_HAS_SM
    if (context->na_sm_event > 0) {
        rc = hg_poll_remove(context->poll_set, context->na_sm_event);
        HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
            HG_NOENTRY, "Could not remove NA SM event from poll set");
        context->na_sm_event = 0;
    }
#endif

    /* Destroy poll set */
    if (context->poll_set != NULL) {
        rc = hg_poll_destroy(context->poll_set);
        HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_FAULT,
            "Could not destroy poll set");
        context->poll_set = NULL;
    }

    /* Destroy NA context */
    if (context->core_context.na_context != NULL) {
        na_return_t na_ret =
            NA_Context_destroy(context->core_context.core_class->na_class,
                context->core_context.na_context);
        HG_CHECK_SUBSYS_ERROR(ctx, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not destroy NA context (%s)",
            NA_Error_to_string(na_ret));
        context->core_context.na_context = NULL;
    }

#ifdef NA_HAS_SM
    /* Destroy NA SM context */
    if (context->core_context.na_sm_context) {
        na_return_t na_ret =
            NA_Context_destroy(context->core_context.core_class->na_sm_class,
                context->core_context.na_sm_context);
        HG_CHECK_SUBSYS_ERROR(ctx, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not destroy NA SM context (%s)",
            NA_Error_to_string(na_ret));
        context->core_context.na_sm_context = NULL;
    }
#endif

    /* Free user data */
    if (context->core_context.data_free_callback)
        context->core_context.data_free_callback(context->core_context.data);

    /* Destroy completion queue mutex/cond */
    (void) hg_thread_mutex_destroy(&backfill_queue->mutex);
    (void) hg_thread_cond_destroy(&backfill_queue->cond);
    (void) hg_thread_mutex_destroy(&context->loopback_notify.mutex);
    (void) hg_thread_spin_destroy(&context->user_list.lock);
    (void) hg_thread_spin_destroy(&context->internal_list.lock);
#ifdef HG_HAS_MULTI_PROGRESS
    (void) hg_thread_mutex_destroy(&progress_multi->mutex);
    (void) hg_thread_cond_destroy(&progress_multi->cond);
#endif

    hg_atomic_queue_free(context->completion_queue);
    free(context);

    /* Decrement context count of parent class */
    hg_atomic_decr32(&hg_core_class->n_contexts);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_post(struct hg_core_private_context *context)
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_CONTEXT_CLASS(context);
    unsigned long flags = HG_CORE_HANDLE_LISTEN;
    hg_return_t ret;

    /* Allocate resources for "listening" on incoming RPCs */
    HG_CHECK_SUBSYS_ERROR(ctx, !hg_core_class->init_info.listen, error, ret,
        HG_OPNOTSUPPORTED, "Cannot post handles on non-listening class");

    /* Allocate multi-recv operations */
    if (hg_core_class->init_info.multi_recv) {
        ret = hg_core_context_multi_recv_alloc(context,
            hg_core_class->core_class.na_class,
            hg_core_class->init_info.request_post_init);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not allocate multi-recv resources");
        flags |= HG_CORE_HANDLE_MULTI_RECV;
        if (hg_core_class->init_info.multi_recv_copy_threshold > 0)
            flags |= HG_CORE_HANDLE_MULTI_RECV_COPY;
    }

    /* Create pool of handles */
    ret = hg_core_handle_pool_create(context,
        hg_core_class->core_class.na_class, context->core_context.na_context,
        flags, hg_core_class->init_info.request_post_init,
        hg_core_class->init_info.request_post_incr, &context->handle_pool);
    HG_CHECK_SUBSYS_HG_ERROR(
        ctx, error, ret, "Could not allocate pool of handles");

#ifdef NA_HAS_SM
    /* Create pool of SM handles */
    if (context->core_context.na_sm_context != NULL) {
        ret = hg_core_handle_pool_create(context,
            hg_core_class->core_class.na_sm_class,
            context->core_context.na_sm_context, HG_CORE_HANDLE_LISTEN,
            hg_core_class->init_info.request_post_init,
            hg_core_class->init_info.request_post_incr,
            &context->sm_handle_pool);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not allocate pool of SM handles");
    }
#endif

    /* Only post multi-recv after pool of handles has been created */
    if (hg_core_class->init_info.multi_recv) {
        ret = hg_core_context_multi_recv_post(context,
            hg_core_class->core_class.na_class,
            context->core_context.na_context);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not post multi-recv operations");
    }

    context->posted = true;

    return HG_SUCCESS;

error:
    if (context->handle_pool != NULL) {
        (void) hg_core_handle_pool_unpost(
            context->handle_pool, HG_CORE_CLEANUP_TIMEOUT);
        hg_core_handle_pool_destroy(context->handle_pool);
    }
#ifdef NA_HAS_SM
    if (context->sm_handle_pool != NULL) {
        (void) hg_core_handle_pool_unpost(
            context->sm_handle_pool, HG_CORE_CLEANUP_TIMEOUT);
        hg_core_handle_pool_destroy(context->sm_handle_pool);
    }
#endif
    if (hg_core_class->init_info.multi_recv)
        hg_core_context_multi_recv_free(
            context, hg_core_class->core_class.na_class);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_unpost(
    struct hg_core_private_context *context, unsigned int timeout_ms)
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_CONTEXT_CLASS(context);
    hg_return_t ret;

    if (!hg_core_class->init_info.listen || !context->posted)
        return HG_SUCCESS;

    /* Prevent requests from being reposted as they complete */
    hg_atomic_set32(&context->unposting, 1);

    if (hg_core_class->init_info.multi_recv) {
        ret = hg_core_context_multi_recv_unpost(context,
            hg_core_class->core_class.na_class,
            context->core_context.na_context);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not unpost multi-recv operations");
    }

    if (context->handle_pool != NULL) {
        ret = hg_core_handle_pool_unpost(context->handle_pool, timeout_ms);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not unpost pool of handles");

        hg_core_handle_pool_destroy(context->handle_pool);
        context->handle_pool = NULL;
    }

#ifdef NA_HAS_SM
    if (context->sm_handle_pool != NULL) {
        ret = hg_core_handle_pool_unpost(context->sm_handle_pool, timeout_ms);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not unpost pool of handles");

        hg_core_handle_pool_destroy(context->sm_handle_pool);
        context->sm_handle_pool = NULL;
    }
#endif

    /* Wait on internal list */
    ret =
        hg_core_context_list_wait(context, &context->internal_list, timeout_ms);
    HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not wait on handle list");

    if (hg_core_class->init_info.multi_recv)
        hg_core_context_multi_recv_free(
            context, hg_core_class->core_class.na_class);

    context->posted = false;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_multi_recv_alloc(struct hg_core_private_context *context,
    na_class_t *na_class, unsigned int request_count)
{
    unsigned int multi_recv_op_max =
        HG_CORE_CONTEXT_CLASS(context)->init_info.multi_recv_op_max;
    size_t unexpected_msg_size;
    hg_return_t ret;
    unsigned int i;

    unexpected_msg_size = NA_Msg_get_max_unexpected_size(na_class);
    HG_CHECK_SUBSYS_ERROR(ctx, unexpected_msg_size == 0, error, ret,
        HG_INVALID_PARAM, "Invalid unexpected message size");
    context->multi_recv_ops =
        calloc(multi_recv_op_max, sizeof(*context->multi_recv_ops));
    HG_CHECK_SUBSYS_ERROR(ctx, context->multi_recv_ops == NULL, error, ret,
        HG_NOMEM, "Could not allocate %u multi-recv op entries",
        multi_recv_op_max);

    for (i = 0; i < multi_recv_op_max; i++) {
        struct hg_core_multi_recv_op *multi_recv_op =
            &context->multi_recv_ops[i];

        multi_recv_op->context = context;
        multi_recv_op->op_id = NA_Op_create(na_class, NA_OP_MULTI);
        HG_CHECK_SUBSYS_ERROR(ctx, multi_recv_op->op_id == NULL, error, ret,
            HG_NOMEM, "Could not create new OP ID");

        /* Keep total buffer size as max of unexpected msg size x number of
         * "pre-posted" operations. */
        multi_recv_op->buf_size = request_count * unexpected_msg_size;

        multi_recv_op->buf = NA_Msg_buf_alloc(na_class, multi_recv_op->buf_size,
            NA_MULTI_RECV, &multi_recv_op->plugin_data);
        HG_CHECK_SUBSYS_ERROR(ctx, multi_recv_op->buf == NULL, error, ret,
            HG_NOMEM, "Could not allocate multi-recv buffer of size %zu",
            multi_recv_op->buf_size);

        hg_atomic_init32(&multi_recv_op->last, 0);
        hg_atomic_init32(&multi_recv_op->ref_count, 0);
        hg_atomic_init32(&multi_recv_op->op_count, 0);
    }

    return HG_SUCCESS;

error:
    if (context->multi_recv_ops == NULL)
        return ret;

    for (i = 0; i < multi_recv_op_max; i++) {
        struct hg_core_multi_recv_op *multi_recv_op =
            &context->multi_recv_ops[i];
        NA_Op_destroy(na_class, multi_recv_op->op_id);
        multi_recv_op->op_id = NULL;
        NA_Msg_buf_free(
            na_class, multi_recv_op->buf, multi_recv_op->plugin_data);
        multi_recv_op->buf = NULL;
        multi_recv_op->plugin_data = NULL;
        multi_recv_op->buf_size = 0;
    }
    free(context->multi_recv_ops);

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_core_context_multi_recv_free(
    struct hg_core_private_context *context, na_class_t *na_class)
{
    unsigned int multi_recv_op_max =
        HG_CORE_CONTEXT_CLASS(context)->init_info.multi_recv_op_max;
    unsigned int i;

    if (context->multi_recv_ops == NULL)
        return;

    for (i = 0; i < multi_recv_op_max; i++) {
        struct hg_core_multi_recv_op *multi_recv_op =
            &context->multi_recv_ops[i];

        HG_CHECK_SUBSYS_WARNING(ctx,
            hg_atomic_get32(&multi_recv_op->ref_count) != 0,
            "Freeing multi-recv operation that is still being referenced "
            "(%" PRId32 ")",
            hg_atomic_get32(&multi_recv_op->ref_count));

        NA_Op_destroy(na_class, multi_recv_op->op_id);
        multi_recv_op->op_id = NULL;
        NA_Msg_buf_free(
            na_class, multi_recv_op->buf, multi_recv_op->plugin_data);
        multi_recv_op->buf = NULL;
        multi_recv_op->plugin_data = NULL;
        multi_recv_op->buf_size = 0;
    }
    free(context->multi_recv_ops);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_multi_recv_post(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context)
{
    unsigned int multi_recv_op_max =
        HG_CORE_CONTEXT_CLASS(context)->init_info.multi_recv_op_max;
    hg_return_t ret;
    unsigned int i;

    /* Ensure we have enough recvs pre-posted so that handles can be re-assigned
     * a new buffer until the previous buffer can be safely re-used once it's
     * consumed. */
    for (i = 0; i < multi_recv_op_max; i++) {
        struct hg_core_multi_recv_op *multi_recv_op =
            &context->multi_recv_ops[i];

        multi_recv_op->id = i;

        ret = hg_core_post_multi(multi_recv_op, na_class, na_context);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not post multi-recv buffer %u", i);
    }
    hg_atomic_init32(
        &context->multi_recv_op_count, (int32_t) multi_recv_op_max);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_multi_recv_unpost(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context)
{
    unsigned int multi_recv_op_max =
        HG_CORE_CONTEXT_CLASS(context)->init_info.multi_recv_op_max;
    hg_return_t ret;
    unsigned int i;

    for (i = 0; i < multi_recv_op_max; i++) {
        struct hg_core_multi_recv_op *multi_recv_op =
            &context->multi_recv_ops[i];
        na_return_t na_ret;

        na_ret = NA_Cancel(na_class, na_context, multi_recv_op->op_id);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_Cancel() of multi-recv op failed (%s)",
            NA_Error_to_string(na_ret));
    }

    while (hg_atomic_get32(&context->multi_recv_op_count) != 0) {
        ret = hg_core_progress_na(na_class, na_context, NULL);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not make progress on NA");
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_check_handle_list(struct hg_core_handle_list *handle_list)
{
    struct hg_core_private_handle *hg_core_handle = NULL;
    hg_return_t ret;

    hg_thread_spin_lock(&handle_list->lock);

    if (LIST_EMPTY(&handle_list->list))
        HG_GOTO_DONE(unlock, ret, HG_SUCCESS);

    LIST_FOREACH (hg_core_handle, &handle_list->list, created) {
        /* TODO ideally we'd want the upper layer to print that */
        if (hg_core_handle->core_handle.data)
            HG_LOG_SUBSYS_ERROR(ctx, "Handle (%p) was not destroyed",
                hg_core_handle->core_handle.data);
        HG_LOG_SUBSYS_DEBUG(
            ctx, "Core handle (%p) was not destroyed", (void *) hg_core_handle);
    }
    ret = HG_BUSY;

unlock:
    hg_thread_spin_unlock(&handle_list->lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_context_list_wait(struct hg_core_private_context *context,
    struct hg_core_handle_list *handle_list, unsigned int timeout_ms)
{
    bool list_empty = false;
    hg_time_t deadline, now = hg_time_from_ms(0);
    unsigned int count = hg_core_completion_count(context);
    hg_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    /* Make first progress pass without waiting to empty trigger queues */
    ret = hg_core_progress_wait(context, 0);
    HG_CHECK_SUBSYS_ERROR_NORET(ctx, ret != HG_SUCCESS && ret != HG_TIMEOUT,
        error, "Could not make progress");

    for (;;) {
        /* Trigger everything we can from HG */
        hg_core_trigger(context, count, NULL);

        /* Make progress until list is empty */
        hg_thread_spin_lock(&handle_list->lock);
        list_empty = LIST_EMPTY(&handle_list->list);
        hg_thread_spin_unlock(&handle_list->lock);
        if (list_empty)
            break;

        /* Gives a chance to always call trigger after progress */
        if (timeout_ms != 0)
            hg_time_get_current_ms(&now);
        if (!hg_time_less(now, deadline))
            break;

        ret = hg_core_progress(context, &count);
        HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not make progress");
    }

    HG_LOG_SUBSYS_DEBUG(ctx, "List empty: %d (timeout=%u ms)", list_empty,
        hg_time_to_ms(hg_time_subtract(deadline, now)));

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
struct hg_bulk_op_pool *
hg_core_context_get_bulk_op_pool(struct hg_core_context *core_context)
{
    return ((struct hg_core_private_context *) core_context)->hg_bulk_op_pool;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_handle_pool_create(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context, unsigned long flags,
    unsigned int init_count, unsigned int incr_count,
    struct hg_core_handle_pool **hg_core_handle_pool_p)
{
    struct hg_core_handle_pool *hg_core_handle_pool = NULL;
    bool pending_list_lock_init = false, extend_mutex_init = false,
         extend_cond_init = false;
    hg_return_t ret;
    unsigned int i;
    int rc;

    HG_LOG_SUBSYS_DEBUG(ctx,
        "Creating pool of handles (init_count=%u, incr_count=%u)", init_count,
        init_count);

    hg_core_handle_pool =
        (struct hg_core_handle_pool *) calloc(1, sizeof(*hg_core_handle_pool));
    HG_CHECK_SUBSYS_ERROR(ctx, hg_core_handle_pool == NULL, error, ret,
        HG_NOMEM, "Could not allocate handle pool");

    LIST_INIT(&hg_core_handle_pool->pending_list.list);
    rc = hg_thread_spin_init(&hg_core_handle_pool->pending_list.lock);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_spin_init() failed");
    pending_list_lock_init = true;

    rc = hg_thread_mutex_init(&hg_core_handle_pool->extend_mutex);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_mutex_init() failed");
    extend_mutex_init = true;
    hg_thread_cond_init(&hg_core_handle_pool->extend_cond);
    HG_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret, HG_NOMEM,
        "hg_thread_cond_init() failed");
    extend_cond_init = true;

    hg_core_handle_pool->count = init_count;
    hg_core_handle_pool->incr_count = incr_count;
    hg_core_handle_pool->extending = false;
    hg_core_handle_pool->context = context;
    hg_core_handle_pool->na_class = na_class;
    hg_core_handle_pool->na_context = na_context;
    hg_core_handle_pool->flags = flags;

    for (i = 0; i < init_count; i++) {
        ret = hg_core_handle_pool_insert(
            context, na_class, na_context, flags, hg_core_handle_pool);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not insert handle %u into pool", i);
    }

    HG_LOG_SUBSYS_DEBUG(
        ctx, "Created handle pool (%p)", (void *) hg_core_handle_pool);

    *hg_core_handle_pool_p = hg_core_handle_pool;

    return HG_SUCCESS;

error:
    if (hg_core_handle_pool != NULL) {
        struct hg_core_private_handle *hg_core_handle;

        hg_core_handle = LIST_FIRST(&hg_core_handle_pool->pending_list.list);
        while (hg_core_handle) {
            struct hg_core_private_handle *hg_core_handle_next =
                LIST_NEXT(hg_core_handle, pending);
            LIST_REMOVE(hg_core_handle, pending);

            /* Prevent re-initialization */
            hg_core_handle->reuse = false;

            /* Destroy handle */
            (void) hg_core_destroy(hg_core_handle);
            hg_core_handle = hg_core_handle_next;
        }

        if (pending_list_lock_init)
            (void) hg_thread_spin_destroy(
                &hg_core_handle_pool->pending_list.lock);
        if (extend_mutex_init)
            (void) hg_thread_mutex_destroy(&hg_core_handle_pool->extend_mutex);
        if (extend_cond_init)
            (void) hg_thread_cond_destroy(&hg_core_handle_pool->extend_cond);

        free(hg_core_handle_pool);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_core_handle_pool_destroy(struct hg_core_handle_pool *hg_core_handle_pool)
{
    struct hg_core_private_handle *hg_core_handle = NULL;

    HG_LOG_DEBUG("Free handle pool (%p)", (void *) hg_core_handle_pool);

    hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);
    hg_core_handle = LIST_FIRST(&hg_core_handle_pool->pending_list.list);
    while (hg_core_handle) {
        struct hg_core_private_handle *hg_core_handle_next =
            LIST_NEXT(hg_core_handle, pending);
        LIST_REMOVE(hg_core_handle, pending);

        /* Prevent re-initialization */
        hg_core_handle->reuse = false;

        /* Destroy handle */
        (void) hg_core_destroy(hg_core_handle);
        hg_core_handle = hg_core_handle_next;
    }
    hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);

    (void) hg_thread_mutex_destroy(&hg_core_handle_pool->extend_mutex);
    (void) hg_thread_cond_destroy(&hg_core_handle_pool->extend_cond);
    (void) hg_thread_spin_destroy(&hg_core_handle_pool->pending_list.lock);

    free(hg_core_handle_pool);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE bool
hg_core_handle_pool_empty(struct hg_core_handle_pool *hg_core_handle_pool)
{
    bool ret;

    hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);
    ret = LIST_EMPTY(&hg_core_handle_pool->pending_list.list);
    hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_handle_pool_get(struct hg_core_handle_pool *hg_core_handle_pool,
    struct hg_core_private_handle **hg_core_handle_p)
{
    struct hg_core_private_handle *hg_core_handle;
    hg_return_t ret;

    do {
        hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);
        hg_core_handle = LIST_FIRST(&hg_core_handle_pool->pending_list.list);
        if (hg_core_handle != NULL) {
            LIST_REMOVE(hg_core_handle, pending);
            hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);
            break;
        }
        hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);

        /* Grow pool when needed */
        ret = hg_core_handle_pool_extend(hg_core_handle_pool);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, error, ret, "Could not extend pool of handles");

    } while (hg_core_handle == NULL);

    *hg_core_handle_p = hg_core_handle;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_handle_pool_extend(struct hg_core_handle_pool *hg_core_handle_pool)
{
    unsigned int i;
    hg_return_t ret;

    /* Create another batch of IDs if empty */
    hg_thread_mutex_lock(&hg_core_handle_pool->extend_mutex);
    if (hg_core_handle_pool->extending) {
        hg_thread_cond_wait(&hg_core_handle_pool->extend_cond,
            &hg_core_handle_pool->extend_mutex);
        hg_thread_mutex_unlock(&hg_core_handle_pool->extend_mutex);
        return HG_SUCCESS;
    }
    hg_core_handle_pool->extending = true;
    hg_thread_mutex_unlock(&hg_core_handle_pool->extend_mutex);

    /* Only a single thread can extend the pool */
    for (i = 0; i < hg_core_handle_pool->incr_count; i++) {
        ret = hg_core_handle_pool_insert(hg_core_handle_pool->context,
            hg_core_handle_pool->na_class, hg_core_handle_pool->na_context,
            hg_core_handle_pool->flags, hg_core_handle_pool);
        HG_CHECK_SUBSYS_HG_ERROR(
            ctx, unlock, ret, "Could not insert handle %u into pool", i);
    }
    hg_core_handle_pool->count += hg_core_handle_pool->incr_count;

unlock:
    hg_thread_mutex_lock(&hg_core_handle_pool->extend_mutex);
    hg_core_handle_pool->extending = false;
    hg_thread_cond_broadcast(&hg_core_handle_pool->extend_cond);
    hg_thread_mutex_unlock(&hg_core_handle_pool->extend_mutex);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_handle_pool_insert(struct hg_core_private_context *context,
    na_class_t *na_class, na_context_t *na_context, unsigned long flags,
    struct hg_core_handle_pool *hg_core_handle_pool)
{
    struct hg_core_private_handle *hg_core_handle = NULL;
    struct hg_core_private_addr *hg_core_addr = NULL;
    hg_return_t ret;
    bool post = false;

    /* Create new handle */
    ret = hg_core_create(context, na_class, na_context, flags, &hg_core_handle);
    HG_CHECK_SUBSYS_HG_ERROR(
        ctx, error, ret, "Could not create HG core handle");

    /* Reset status */
    hg_atomic_set32(&hg_core_handle->status, 0);
    hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS);

    /* Create new (empty) source addresses */
    ret = hg_core_addr_create(HG_CORE_CONTEXT_CLASS(context), &hg_core_addr);
    HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not create HG addr");
    hg_core_handle->core_handle.info.addr = (hg_core_addr_t) hg_core_addr;

    /* Re-use handle on completion */
    hg_core_handle->reuse = true;

    /* Add handle to pending list */
    hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);
    LIST_INSERT_HEAD(
        &hg_core_handle_pool->pending_list.list, hg_core_handle, pending);
    hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);

    /* Handle is pre-posted only when muti-recv is off */
    if (!(flags & HG_CORE_HANDLE_MULTI_RECV)) {
        /* Handle will need to be posted */
        post = true;

        ret = hg_core_post(hg_core_handle);
        HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not post handle (%p)",
            (void *) hg_core_handle);
    }

    return HG_SUCCESS;

error:
    if (hg_core_handle != NULL) {
        if (post) {
            hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);
            LIST_REMOVE(hg_core_handle, pending);
            hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);
        }
        hg_core_handle->reuse = false;
        (void) hg_core_destroy(hg_core_handle);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_handle_pool_unpost(
    struct hg_core_handle_pool *hg_core_handle_pool, unsigned int timeout_ms)
{
    struct hg_core_private_handle *hg_core_handle;
    hg_return_t ret;

    if (hg_core_handle_pool->flags & HG_CORE_HANDLE_MULTI_RECV)
        return HG_SUCCESS; /* Nothing to do */

    /* Check pending list and cancel posted handles */
    hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);

    LIST_FOREACH (
        hg_core_handle, &hg_core_handle_pool->pending_list.list, pending) {
        /* Cancel handle */
        ret = hg_core_cancel(hg_core_handle);
        HG_CHECK_SUBSYS_HG_ERROR(ctx, unlock, ret,
            "Could not cancel handle (%p)", (void *) hg_core_handle);
    }

    hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);

    /* Check that operations have completed */
    ret = hg_core_context_list_wait(hg_core_handle_pool->context,
        &hg_core_handle_pool->pending_list, timeout_ms);
    HG_CHECK_SUBSYS_HG_ERROR(
        ctx, error, ret, "Could not wait on pool handle list");

    return HG_SUCCESS;

unlock:
    hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE unsigned int
hg_core_map_hash(hg_hash_table_key_t key)
{
    return *((hg_id_t *) key) & 0xffffffff;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE int
hg_core_map_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    return *((hg_id_t *) key1) == *((hg_id_t *) key2);
}

/*---------------------------------------------------------------------------*/
static void
hg_core_map_value_free(hg_hash_table_value_t value)
{
    struct hg_core_rpc_info *hg_core_rpc_info =
        (struct hg_core_rpc_info *) value;

    if (hg_core_rpc_info->free_callback)
        hg_core_rpc_info->free_callback(hg_core_rpc_info->data);
    free(hg_core_rpc_info);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE struct hg_core_rpc_info *
hg_core_map_lookup(struct hg_core_map *hg_core_map, hg_id_t *id)
{
    hg_hash_table_value_t value = NULL;

    /* Lookup key */
    hg_thread_rwlock_rdlock(&hg_core_map->lock);
    value = hg_hash_table_lookup(hg_core_map->map, (hg_hash_table_key_t) id);
    hg_thread_rwlock_release_rdlock(&hg_core_map->lock);

    return (value == HG_HASH_TABLE_NULL) ? NULL
                                         : (struct hg_core_rpc_info *) value;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_map_insert(struct hg_core_map *hg_core_map, hg_id_t *id,
    struct hg_core_rpc_info **hg_core_rpc_info_p)
{
    struct hg_core_rpc_info *hg_core_rpc_info;
    hg_return_t ret;
    int rc;

    /* Allocate new RPC info */
    hg_core_rpc_info =
        (struct hg_core_rpc_info *) calloc(1, sizeof(*hg_core_rpc_info));
    HG_CHECK_SUBSYS_ERROR(cls, hg_core_rpc_info == NULL, error, ret, HG_NOMEM,
        "Could not allocate HG core RPC info");
    hg_core_rpc_info->id = *id;

    hg_thread_rwlock_wrlock(&hg_core_map->lock);
    rc = hg_hash_table_insert(hg_core_map->map,
        (hg_hash_table_key_t) &hg_core_rpc_info->id,
        (hg_hash_table_value_t) hg_core_rpc_info);
    hg_thread_rwlock_release_wrlock(&hg_core_map->lock);
    HG_CHECK_SUBSYS_ERROR(
        cls, rc == 0, error, ret, HG_NOMEM, "hg_hash_table_insert() failed");

    *hg_core_rpc_info_p = hg_core_rpc_info;

    return HG_SUCCESS;

error:
    free(hg_core_rpc_info);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_map_remove(struct hg_core_map *hg_core_map, hg_id_t *id)
{
    hg_return_t ret;
    int rc;

    /* Remove key */
    hg_thread_rwlock_wrlock(&hg_core_map->lock);
    rc = hg_hash_table_remove(hg_core_map->map, (hg_hash_table_key_t) id);
    hg_thread_rwlock_release_wrlock(&hg_core_map->lock);
    HG_CHECK_SUBSYS_ERROR(
        cls, rc != 1, error, ret, HG_NOENTRY, "hg_hash_table_remove() failed");

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_lookup(struct hg_core_private_class *hg_core_class,
    const char *name, struct hg_core_private_addr **addr_p)
{
    struct hg_core_private_addr *hg_core_addr = NULL;
    na_class_t **na_class_p = NULL;
    na_addr_t **na_addr_p = NULL;
    size_t *na_addr_serialize_size_p = NULL;
    na_return_t na_ret;
    const char *name_str = NULL;
    hg_return_t ret;

    /* Allocate addr */
    ret = hg_core_addr_create(hg_core_class, &hg_core_addr);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret, "Could not create HG core addr");

    /* TODO lookup could also create self addresses */

#ifdef NA_HAS_SM
    if (hg_core_class->core_class.na_sm_class != NULL)
        name_str = strstr(name, HG_CORE_ADDR_DELIMITER);

    /* Parse name string */
    if (name_str != NULL) {
        char uuid_str[NA_SM_HOST_ID_LEN + 1];
        int rc;

        /* Get first part of address string with host ID */
        rc = sscanf(name, "uid://%11[^#]", uuid_str);
        HG_CHECK_SUBSYS_ERROR(addr, rc != 1, error, ret, HG_PROTONOSUPPORT,
            "Malformed address format (%s)", name);

        na_ret = NA_SM_String_to_host_id(uuid_str, &hg_core_addr->host_id);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_SM_String_to_host_id() failed (%s)",
            NA_Error_to_string(na_ret));

        /* Skip delimiter */
        name_str += HG_CORE_ADDR_DELIMITER_LEN;

        /* Compare IDs, if they match it's local address */
        if (NA_SM_Host_id_cmp(hg_core_addr->host_id, hg_core_class->host_id)) {
            HG_LOG_SUBSYS_DEBUG(addr, "%s is a local address", name);
            na_class_p = &hg_core_addr->core_addr.core_class->na_sm_class;
            na_addr_p = &hg_core_addr->core_addr.na_sm_addr;
            na_addr_serialize_size_p = &hg_core_addr->na_sm_addr_serialize_size;
        } else {
            /* Remote lookup */
            name_str = strstr(name_str, HG_CORE_ADDR_DELIMITER);
            HG_CHECK_SUBSYS_ERROR(addr, name_str == NULL, error, ret,
                HG_PROTONOSUPPORT, "Malformed remote address string (%s)",
                name);

            na_class_p = &hg_core_addr->core_addr.core_class->na_class;
            na_addr_p = &hg_core_addr->core_addr.na_addr;
            na_addr_serialize_size_p = &hg_core_addr->na_addr_serialize_size;
            name_str += HG_CORE_ADDR_DELIMITER_LEN;
        }
    } else {
#endif
        /* Remote lookup */
        na_class_p = &hg_core_addr->core_addr.core_class->na_class;
        na_addr_p = &hg_core_addr->core_addr.na_addr;
        na_addr_serialize_size_p = &hg_core_addr->na_addr_serialize_size;
        name_str = name;
#ifdef NA_HAS_SM
    }
#endif

    /* Lookup adress */
    na_ret = NA_Addr_lookup(*na_class_p, name_str, na_addr_p);
    HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "Could not lookup address %s (%s)", name_str,
        NA_Error_to_string(na_ret));

    /* Cache serialize size */
    *na_addr_serialize_size_p =
        NA_Addr_get_serialize_size(*na_class_p, *na_addr_p);

    *addr_p = hg_core_addr;

    return HG_SUCCESS;

error:
    hg_core_addr_free(hg_core_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_create(struct hg_core_private_class *hg_core_class,
    struct hg_core_private_addr **hg_core_addr_p)
{
    struct hg_core_private_addr *hg_core_addr = NULL;
    hg_return_t ret;

    hg_core_addr =
        (struct hg_core_private_addr *) calloc(1, sizeof(*hg_core_addr));
    HG_CHECK_SUBSYS_ERROR(addr, hg_core_addr == NULL, error, ret, HG_NOMEM,
        "Could not allocate HG core addr");

    hg_core_addr->core_addr.core_class = (hg_core_class_t *) hg_core_class;
    hg_core_addr->core_addr.na_addr = NULL;
#ifdef NA_HAS_SM
    hg_core_addr->core_addr.na_sm_addr = NULL;
#endif
    hg_core_addr->core_addr.is_self = false;

    hg_atomic_init32(&hg_core_addr->ref_count, 1);

    /* Increment N addrs from HG class */
    hg_atomic_incr32(&hg_core_class->n_addrs);

    *hg_core_addr_p = hg_core_addr;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_core_addr_free(struct hg_core_private_addr *hg_core_addr)
{
    struct hg_core_private_class *hg_core_class;

    if (hg_core_addr == NULL)
        return;

    if (hg_atomic_decr32(&hg_core_addr->ref_count))
        /* Cannot free yet */
        return;

    /* Keep reference to core class */
    hg_core_class = HG_CORE_ADDR_CLASS(hg_core_addr);

    /* Free NA addresses */
    hg_core_addr_free_na(hg_core_addr);

    free(hg_core_addr);

    /* Decrement N addrs from HG class */
    hg_atomic_decr32(&hg_core_class->n_addrs);
}

/*---------------------------------------------------------------------------*/
static void
hg_core_addr_free_na(struct hg_core_private_addr *hg_core_addr)
{
    /* Free NA address */
    if (hg_core_addr->core_addr.na_addr != NULL) {
        NA_Addr_free(hg_core_addr->core_addr.core_class->na_class,
            hg_core_addr->core_addr.na_addr);
        hg_core_addr->core_addr.na_addr = NULL;
        hg_core_addr->na_addr_serialize_size = 0;
    }

#ifdef NA_HAS_SM
    /* Free NA SM address */
    if (hg_core_addr->core_addr.na_sm_addr != NULL) {
        NA_Addr_free(hg_core_addr->core_addr.core_class->na_sm_class,
            hg_core_addr->core_addr.na_sm_addr);
        hg_core_addr->core_addr.na_sm_addr = NULL;
        hg_core_addr->na_sm_addr_serialize_size = 0;
    }
#endif
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_set_remove(struct hg_core_private_addr *hg_core_addr)
{
    hg_return_t ret;

    if (hg_core_addr->core_addr.na_addr != NULL) {
        na_return_t na_ret =
            NA_Addr_set_remove(hg_core_addr->core_addr.core_class->na_class,
                hg_core_addr->core_addr.na_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_Addr_set_remove() failed (%s)",
            NA_Error_to_string(na_ret));
    }

#ifdef NA_HAS_SM
    if (hg_core_addr->core_addr.na_sm_addr != NULL) {
        na_return_t na_ret =
            NA_Addr_set_remove(hg_core_addr->core_addr.core_class->na_sm_class,
                hg_core_addr->core_addr.na_sm_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_Addr_set_remove() failed (%s)",
            NA_Error_to_string(na_ret));
    }
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_self(struct hg_core_private_class *hg_core_class,
    struct hg_core_private_addr **hg_core_addr_p)
{
    struct hg_core_private_addr *hg_core_addr = NULL;
    hg_return_t ret;
    na_return_t na_ret;

    ret = hg_core_addr_create(hg_core_class, &hg_core_addr);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret, "Could not create HG core addr");
    hg_core_addr->core_addr.is_self = true;

    /* Get NA address */
    na_ret = NA_Addr_self(
        hg_core_class->core_class.na_class, &hg_core_addr->core_addr.na_addr);
    HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "Could not get self address (%s)",
        NA_Error_to_string(na_ret));

    /* Cache serialize size */
    hg_core_addr->na_addr_serialize_size = NA_Addr_get_serialize_size(
        hg_core_class->core_class.na_class, hg_core_addr->core_addr.na_addr);

#ifdef NA_HAS_SM
    if (hg_core_class->core_class.na_sm_class) {
        /* Get SM address */
        na_ret = NA_Addr_self(hg_core_class->core_class.na_sm_class,
            &hg_core_addr->core_addr.na_sm_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not get self SM address (%s)",
            NA_Error_to_string(na_ret));

        /* Cache serialize size */
        hg_core_addr->na_sm_addr_serialize_size =
            NA_Addr_get_serialize_size(hg_core_class->core_class.na_sm_class,
                hg_core_addr->core_addr.na_sm_addr);

        /* Copy local host ID */
        NA_SM_Host_id_copy(&hg_core_addr->host_id, hg_core_class->host_id);
    }
#endif

    *hg_core_addr_p = hg_core_addr;

    return HG_SUCCESS;

error:
    hg_core_addr_free(hg_core_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_dup(struct hg_core_private_addr *hg_core_addr,
    struct hg_core_private_addr **hg_core_addr_p)
{
    struct hg_core_private_addr *hg_new_addr = NULL;
    hg_return_t ret;

    ret = hg_core_addr_create(HG_CORE_ADDR_CLASS(hg_core_addr), &hg_new_addr);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret, "Could not create HG core addr");
    hg_new_addr->core_addr.is_self = hg_core_addr->core_addr.is_self;

    if (hg_core_addr->core_addr.na_addr != NULL) {
        na_return_t na_ret = NA_Addr_dup(
            hg_core_addr->core_addr.core_class->na_class,
            hg_core_addr->core_addr.na_addr, &hg_new_addr->core_addr.na_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not duplicate address (%s)",
            NA_Error_to_string(na_ret));

        /* Copy serialize size */
        hg_new_addr->na_addr_serialize_size =
            hg_core_addr->na_addr_serialize_size;
    }

#ifdef NA_HAS_SM
    if (hg_core_addr->core_addr.na_sm_addr != NULL) {
        na_return_t na_ret =
            NA_Addr_dup(hg_core_addr->core_addr.core_class->na_sm_class,
                hg_core_addr->core_addr.na_sm_addr,
                &hg_new_addr->core_addr.na_sm_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not duplicate address (%s)",
            NA_Error_to_string(na_ret));

        /* Copy serialize size */
        hg_new_addr->na_sm_addr_serialize_size =
            hg_core_addr->na_sm_addr_serialize_size;

        /* Copy local host ID */
        NA_SM_Host_id_copy(&hg_new_addr->host_id, hg_core_addr->host_id);
    }
#endif

    *hg_core_addr_p = hg_new_addr;

    return HG_SUCCESS;

error:
    hg_core_addr_free(hg_new_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static bool
hg_core_addr_cmp(
    struct hg_core_private_addr *addr1, struct hg_core_private_addr *addr2)
{
    bool ret = true;

    /* Cannot be separate classes */
    if (addr1->core_addr.core_class != addr2->core_addr.core_class)
        return false;

    /* Self addresses are always equal */
    if (addr1->core_addr.is_self && addr2->core_addr.is_self)
        return true;

    /* Compare NA addresses */
    ret &= NA_Addr_cmp(addr1->core_addr.core_class->na_class,
        addr1->core_addr.na_addr, addr2->core_addr.na_addr);

#ifdef NA_HAS_SM
    /* Compare NA SM addresses */
    if (addr1->core_addr.core_class->na_sm_class)
        ret &= NA_Addr_cmp(addr1->core_addr.core_class->na_sm_class,
            addr1->core_addr.na_sm_addr, addr2->core_addr.na_sm_addr);
#endif

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_to_string(
    char *buf, hg_size_t *buf_size, struct hg_core_private_addr *hg_core_addr)
{
    na_class_t *na_class = hg_core_addr->core_addr.core_class->na_class;
    na_addr_t *na_addr = hg_core_addr->core_addr.na_addr;
    char *buf_ptr = buf;
    size_t new_buf_size = 0, buf_size_used = 0;
    hg_return_t ret;
    na_return_t na_ret;

    new_buf_size = (size_t) *buf_size;

#ifdef NA_HAS_SM
    /* When we have local and remote addresses */
    if ((hg_core_addr->core_addr.na_sm_addr != NULL) &&
        (hg_core_addr->core_addr.na_addr != NULL)) {
        char addr_str[HG_CORE_ADDR_MAX_SIZE];
        char uuid_str[NA_SM_HOST_ID_LEN + 1];
        int desc_len;

        /* Convert host ID to string and generate addr string */
        na_ret = NA_SM_Host_id_to_string(hg_core_addr->host_id, uuid_str);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_SM_Host_id_to_string() failed (%s)",
            NA_Error_to_string(na_ret));

        desc_len = snprintf(addr_str, HG_CORE_ADDR_MAX_SIZE,
            "uid://%s" HG_CORE_ADDR_DELIMITER, uuid_str);
        HG_CHECK_SUBSYS_ERROR(addr, desc_len > HG_CORE_ADDR_MAX_SIZE, error,
            ret, HG_OVERFLOW, "Exceeding max addr name");

        if (buf_ptr) {
            strcpy(buf_ptr, addr_str);
            buf_ptr += desc_len;
        }
        buf_size_used += (hg_size_t) desc_len;
        if (*buf_size > (unsigned int) desc_len)
            new_buf_size = *buf_size - (size_t) desc_len;

        /* Get NA SM address string */
        na_ret =
            NA_Addr_to_string(hg_core_addr->core_addr.core_class->na_sm_class,
                buf_ptr, &new_buf_size, hg_core_addr->core_addr.na_sm_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not convert SM address to string (%s)",
            NA_Error_to_string(na_ret));

        if (buf_ptr) {
            buf_ptr[new_buf_size - 1] = *HG_CORE_ADDR_DELIMITER;
            buf_ptr += new_buf_size;
        }
        buf_size_used += new_buf_size;
        if (*buf_size > new_buf_size)
            new_buf_size = *buf_size - new_buf_size;
    } else if (hg_core_addr->core_addr.na_sm_addr != NULL) {
        na_class = hg_core_addr->core_addr.core_class->na_sm_class;
        na_addr = hg_core_addr->core_addr.na_sm_addr;
    }
#endif

    /* Get NA address string */
    na_ret = NA_Addr_to_string(na_class, buf_ptr, &new_buf_size, na_addr);
    HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "Could not convert address (%p) to string (%s)",
        (void *) na_addr, NA_Error_to_string(na_ret));

    *buf_size = (hg_size_t) (new_buf_size + buf_size_used);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_size_t
hg_core_addr_get_serialize_size(
    struct hg_core_private_addr *hg_core_addr, uint8_t flags)
{
    hg_size_t ret = sizeof(size_t);

    if (hg_core_addr->core_addr.na_addr != NULL) {
        if (hg_core_addr->na_addr_serialize_size == 0) {
            /* Cache serialize size */
            hg_core_addr->na_addr_serialize_size = NA_Addr_get_serialize_size(
                hg_core_addr->core_addr.core_class->na_class,
                hg_core_addr->core_addr.na_addr);
        }

        ret += hg_core_addr->na_addr_serialize_size;
    }

#ifdef NA_HAS_SM
    ret += sizeof(size_t);

    if ((flags & HG_CORE_SM) && hg_core_addr->core_addr.na_sm_addr != NULL) {
        if (hg_core_addr->na_sm_addr_serialize_size == 0) {
            /* Cache serialize size */
            hg_core_addr->na_sm_addr_serialize_size =
                NA_Addr_get_serialize_size(
                    hg_core_addr->core_addr.core_class->na_sm_class,
                    hg_core_addr->core_addr.na_sm_addr);
        }

        ret += hg_core_addr->na_sm_addr_serialize_size +
               sizeof(hg_core_addr->host_id);
    }
#else
    (void) flags;
#endif

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_serialize(void *buf, hg_size_t buf_size, uint8_t flags,
    struct hg_core_private_addr *hg_core_addr)
{
    char *buf_ptr = (char *) buf;
    hg_size_t buf_size_left = buf_size;
    hg_return_t ret;

    if (hg_core_addr->core_addr.na_addr != NULL) {
        na_return_t na_ret;

        HG_CORE_ENCODE(addr, error, ret, buf_ptr, buf_size_left,
            &hg_core_addr->na_addr_serialize_size, size_t);

        na_ret = NA_Addr_serialize(hg_core_addr->core_addr.core_class->na_class,
            buf_ptr, buf_size_left, hg_core_addr->core_addr.na_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not serialize NA address (%s)",
            NA_Error_to_string(na_ret));
        buf_ptr += hg_core_addr->na_addr_serialize_size;
        buf_size_left -= hg_core_addr->na_addr_serialize_size;
    } else {
        size_t na_sm_addr_serialize_size = 0;

        /* Encode a 0 instead of flag */
        HG_CORE_ENCODE(addr, error, ret, buf_ptr, buf_size_left,
            &na_sm_addr_serialize_size, size_t);
    }

#ifdef NA_HAS_SM
    if ((flags & HG_CORE_SM) && hg_core_addr->core_addr.na_sm_addr != NULL) {
        na_return_t na_ret;

        HG_CORE_ENCODE(addr, error, ret, buf_ptr, buf_size_left,
            &hg_core_addr->na_sm_addr_serialize_size, size_t);

        na_ret =
            NA_Addr_serialize(hg_core_addr->core_addr.core_class->na_sm_class,
                buf_ptr, buf_size_left, hg_core_addr->core_addr.na_sm_addr);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not serialize NA SM address (%s)",
            NA_Error_to_string(na_ret));
        /*
        buf_ptr += hg_core_addr->na_sm_addr_serialize_size;
        buf_size_left -= hg_core_addr->na_sm_addr_serialize_size;
*/
    } else {
        size_t na_sm_addr_serialize_size = 0;

        /* Encode a 0 instead of flag */
        HG_CORE_ENCODE(addr, error, ret, buf_ptr, buf_size_left,
            &na_sm_addr_serialize_size, size_t);
    }
#else
    (void) flags;
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_addr_deserialize(struct hg_core_private_class *hg_core_class,
    struct hg_core_private_addr **hg_core_addr_p, const void *buf,
    hg_size_t buf_size)
{
    struct hg_core_private_addr *hg_core_addr = NULL;
    const char *buf_ptr = (const char *) buf;
    hg_size_t buf_size_left = buf_size;
    bool is_self = true;
    hg_return_t ret;

    /* Create new address */
    ret = hg_core_addr_create(hg_core_class, &hg_core_addr);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret, "Could not create HG core addr");

    HG_CORE_DECODE(addr, error, ret, buf_ptr, buf_size_left,
        &hg_core_addr->na_addr_serialize_size, size_t);

    if (hg_core_addr->na_addr_serialize_size != 0) {
        na_return_t na_ret =
            NA_Addr_deserialize(hg_core_class->core_class.na_class,
                &hg_core_addr->core_addr.na_addr, buf_ptr, buf_size_left);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not deserialize NA address (%s)",
            NA_Error_to_string(na_ret));
        buf_ptr += hg_core_addr->na_addr_serialize_size;
        buf_size_left -= hg_core_addr->na_addr_serialize_size;

        is_self &= NA_Addr_is_self(hg_core_class->core_class.na_class,
            hg_core_addr->core_addr.na_addr);
    }

#ifdef NA_HAS_SM
    HG_CORE_DECODE(addr, error, ret, buf_ptr, buf_size_left,
        &hg_core_addr->na_sm_addr_serialize_size, size_t);

    if (hg_core_addr->na_sm_addr_serialize_size != 0) {
        na_return_t na_ret =
            NA_Addr_deserialize(hg_core_class->core_class.na_sm_class,
                &hg_core_addr->core_addr.na_sm_addr, buf_ptr, buf_size_left);
        HG_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not deserialize NA SM address (%s)",
            NA_Error_to_string(na_ret));
        /*
        buf_ptr += hg_core_addr->na_sm_addr_serialize_size;
        buf_size_left -= hg_core_addr->na_sm_addr_serialize_size;
        */
        is_self &= NA_Addr_is_self(hg_core_class->core_class.na_class,
            hg_core_addr->core_addr.na_addr);
    }
#endif
    hg_core_addr->core_addr.is_self = is_self;

    *hg_core_addr_p = hg_core_addr;

    return HG_SUCCESS;

error:
    hg_core_addr_free(hg_core_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_resolve_na(struct hg_core_private_context *context,
    struct hg_core_private_addr *hg_core_addr, na_class_t **na_class_p,
    na_context_t **na_context_p, na_addr_t **na_addr_p)
{
    hg_return_t ret;

    /* Determine which NA class/context to use */
    if (hg_core_addr != NULL) {
        HG_CHECK_SUBSYS_ERROR(addr,
            hg_core_addr->core_addr.core_class !=
                context->core_context.core_class,
            error, ret, HG_INVALID_ARG,
            "Address and context passed belong to different classes");

#ifdef NA_HAS_SM
        if (!hg_core_addr->core_addr.is_self &&
            (hg_core_addr->core_addr.na_sm_addr != NULL)) {
            HG_LOG_SUBSYS_DEBUG(rpc, "Using NA SM class");
            *na_class_p = context->core_context.core_class->na_sm_class;
            *na_context_p = context->core_context.na_sm_context;
            *na_addr_p = hg_core_addr->core_addr.na_sm_addr;
        } else {
#endif
            /* Default */
            HG_LOG_SUBSYS_DEBUG(rpc, "Using default NA class");
            *na_class_p = context->core_context.core_class->na_class;
            *na_context_p = context->core_context.na_context;
            *na_addr_p = hg_core_addr->core_addr.na_addr;
#ifdef NA_HAS_SM
        }
#endif
    } else {
        /* Default */
        HG_LOG_SUBSYS_DEBUG(rpc, "Using default NA class");
        *na_class_p = context->core_context.core_class->na_class;
        *na_context_p = context->core_context.na_context;
        *na_addr_p = NULL;
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_create(struct hg_core_private_context *context, na_class_t *na_class,
    na_context_t *na_context, unsigned long flags,
    struct hg_core_private_handle **hg_core_handle_p)
{
    struct hg_core_private_handle *hg_core_handle = NULL;
    hg_return_t ret;

    /* Allocate new handle */
    ret = hg_core_alloc(context, flags & HG_CORE_HANDLE_USER, &hg_core_handle);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not allocate handle");

    /* Alloc/init NA resources */
    ret = hg_core_alloc_na(hg_core_handle, na_class, na_context, flags);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not allocate NA handle resources");

    /* Execute class callback on handle, this allows upper layers to
     * allocate private data on handle creation */
    if (context->handle_create_cb.callback) {
        ret = context->handle_create_cb.callback(
            (hg_core_handle_t) hg_core_handle, context->handle_create_cb.arg);
        HG_CHECK_SUBSYS_HG_ERROR(
            rpc, error, ret, "Error in HG handle create callback");
    }

    HG_LOG_SUBSYS_DEBUG(
        rpc, "Created new handle (%p)", (void *) hg_core_handle);

    *hg_core_handle_p = hg_core_handle;

    return HG_SUCCESS;

error:
    (void) hg_core_destroy(hg_core_handle);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_destroy(struct hg_core_private_handle *hg_core_handle)
{
    int32_t ref_count, flags, no_response_done = 0;
    hg_return_t ret;

    if (hg_core_handle == NULL)
        return HG_SUCCESS;

    /* Retrieve flags before decrementing refcount */
    flags = hg_atomic_get32(&hg_core_handle->flags);
    if ((flags & HG_CORE_SELF_FORWARD) && (flags & HG_CORE_NO_RESPONSE))
        no_response_done = hg_atomic_get32(&hg_core_handle->no_response_done);

    /* Standard destroy refcount decrement */
    ref_count = hg_atomic_decr32(&hg_core_handle->ref_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count decr to %" PRId32,
        (void *) hg_core_handle, ref_count);

    if (ref_count > 0) {
        /* This will push the RPC handle back to completion queue when no
         * response is sent and we are sending to ourselves. This ensures that
         * there is no race between the callback execution and the RPC
         * completion. */
        if (ref_count == no_response_done)
            hg_core_no_respond_self(hg_core_handle);

        return HG_SUCCESS; /* Cannot free yet */
    }

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    if (hg_core_handle->active) {
        hg_atomic_decr64(HG_CORE_HANDLE_CLASS(hg_core_handle)
                             ->counters.rpc_req_recv_active_count);
        hg_core_handle->active = false;
    }
#endif

    /* Re-use handle if we were listening, otherwise destroy it */
    if (hg_core_handle->reuse &&
        !hg_atomic_get32(&HG_CORE_HANDLE_CONTEXT(hg_core_handle)->unposting)) {
        HG_LOG_SUBSYS_DEBUG(
            rpc, "Re-using handle (%p)", (void *) hg_core_handle);

        /* Repost handle */
        ret = hg_core_reset_post(hg_core_handle);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Cannot re-use handle (%p)",
            (void *) hg_core_handle);

        /* TODO handle error */
    } else {
        struct hg_core_private_class *hg_core_class =
            HG_CORE_HANDLE_CLASS(hg_core_handle);

        HG_LOG_SUBSYS_DEBUG(
            rpc, "Freeing handle (%p)", (void *) hg_core_handle);

        /* Free extra data here if needed */
        if (hg_core_class->more_data_cb.release)
            hg_core_class->more_data_cb.release(
                (hg_core_handle_t) hg_core_handle);

        /* Free user data */
        if (hg_core_handle->core_handle.data_free_callback)
            hg_core_handle->core_handle.data_free_callback(
                hg_core_handle->core_handle.data);

        /* Free NA resources */
        if (hg_core_handle->na_class)
            hg_core_free_na(hg_core_handle);

        /* Free handle */
        hg_core_free(hg_core_handle);
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_alloc(struct hg_core_private_context *context, bool user,
    struct hg_core_private_handle **hg_core_handle_p)
{
    hg_checksum_level_t checksum_level =
        HG_CORE_CONTEXT_CLASS(context)->init_info.checksum_level;
    struct hg_core_private_handle *hg_core_handle = NULL;
    hg_return_t ret;

    hg_core_handle =
        (struct hg_core_private_handle *) calloc(1, sizeof(*hg_core_handle));
    HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle == NULL, error, ret, HG_NOMEM,
        "Could not allocate handle");

    hg_core_handle->op_type = HG_CORE_PROCESS; /* Default */
    hg_core_handle->core_handle.info.core_class =
        context->core_context.core_class;
    hg_core_handle->core_handle.info.context = &context->core_context;
    hg_core_handle->core_handle.info.addr = HG_CORE_ADDR_NULL;

    /* Default ops */
    hg_core_handle->ops = hg_core_ops_na_g;

    /* Default return code */
    hg_core_handle->ret = HG_SUCCESS;

    /* Add handle to handle list so that we can track it */
    hg_core_handle->created_list =
        (user) ? &context->user_list : &context->internal_list;
    hg_thread_spin_lock(&hg_core_handle->created_list->lock);
    LIST_INSERT_HEAD(
        &hg_core_handle->created_list->list, hg_core_handle, created);
    hg_thread_spin_unlock(&hg_core_handle->created_list->lock);

    /* Completed by default */
    hg_atomic_init32(&hg_core_handle->status, HG_CORE_OP_COMPLETED);
    hg_atomic_init32(
        &hg_core_handle->ret_status, (int32_t) hg_core_handle->ret);

    /* Init in/out header */
    hg_core_header_request_init(
        &hg_core_handle->in_header, checksum_level > HG_CHECKSUM_NONE);
    hg_core_header_response_init(
        &hg_core_handle->out_header, checksum_level > HG_CHECKSUM_NONE);

    /* Set refcount to 1 */
    hg_atomic_init32(&hg_core_handle->ref_count, 1);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count set to %" PRId32,
        (void *) hg_core_handle, 1);

    /* Increment N handles from HG context */
    hg_atomic_incr32(&context->n_handles);

    *hg_core_handle_p = hg_core_handle;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_core_free(struct hg_core_private_handle *hg_core_handle)
{
    /* Remove reference to HG addr */
    hg_core_addr_free(
        (struct hg_core_private_addr *) hg_core_handle->core_handle.info.addr);

    /* Remove handle from list */
    hg_thread_spin_lock(&hg_core_handle->created_list->lock);
    LIST_REMOVE(hg_core_handle, created);
    hg_thread_spin_unlock(&hg_core_handle->created_list->lock);

    hg_core_header_request_finalize(&hg_core_handle->in_header);
    hg_core_header_response_finalize(&hg_core_handle->out_header);

    /* Decrement N handles from HG context */
    hg_atomic_decr32(&HG_CORE_HANDLE_CONTEXT(hg_core_handle)->n_handles);

    free(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_alloc_na(struct hg_core_private_handle *hg_core_handle,
    na_class_t *na_class, na_context_t *na_context, unsigned long flags)
{
    hg_return_t ret;
    na_return_t na_ret;

    /* Set NA class / context */
    hg_core_handle->na_class = na_class;
    hg_core_handle->na_context = na_context;

    /* When using multi-recv, only allocate resources per handle for expected
     * messages. */
    if (flags & HG_CORE_HANDLE_MULTI_RECV) {
        if (flags & HG_CORE_HANDLE_MULTI_RECV_COPY) {
            hg_core_handle->in_buf_storage_size =
                NA_Msg_get_max_unexpected_size(na_class);

            hg_core_handle->in_buf_storage =
                NA_Msg_buf_alloc(na_class, hg_core_handle->in_buf_storage_size,
                    NA_RECV, &hg_core_handle->in_buf_plugin_data);
            HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->in_buf_storage == NULL,
                error, ret, HG_NOMEM, "Could not allocate buffer for input");
        }
        hg_core_handle->core_handle.in_buf = NULL;
        hg_core_handle->core_handle.in_buf_size = 0;
    } else {
        /* Initialize in/out buffers and use unexpected message size */
        hg_core_handle->in_buf_storage_size =
            NA_Msg_get_max_unexpected_size(na_class);

        hg_core_handle->in_buf_storage =
            NA_Msg_buf_alloc(na_class, hg_core_handle->in_buf_storage_size,
                (flags & HG_CORE_HANDLE_LISTEN) ? NA_RECV : NA_SEND,
                &hg_core_handle->in_buf_plugin_data);
        HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->in_buf_storage == NULL,
            error, ret, HG_NOMEM, "Could not allocate buffer for input");

        hg_core_handle->core_handle.in_buf = hg_core_handle->in_buf_storage;
        hg_core_handle->core_handle.in_buf_size =
            hg_core_handle->in_buf_storage_size;

        na_ret =
            NA_Msg_init_unexpected(na_class, hg_core_handle->in_buf_storage,
                hg_core_handle->in_buf_storage_size);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not initialize input buffer (%s)",
            NA_Error_to_string(na_ret));

        hg_core_handle->na_recv_op_id = NA_Op_create(na_class, NA_OP_SINGLE);
        HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->na_recv_op_id == NULL, error,
            ret, HG_NA_ERROR, "Could not create NA op ID");
    }

    hg_core_handle->core_handle.out_buf_size =
        NA_Msg_get_max_expected_size(na_class);

    hg_core_handle->core_handle.na_in_header_offset =
        NA_Msg_get_unexpected_header_size(na_class);
    hg_core_handle->core_handle.na_out_header_offset =
        NA_Msg_get_expected_header_size(na_class);

    hg_core_handle->core_handle.out_buf =
        NA_Msg_buf_alloc(na_class, hg_core_handle->core_handle.out_buf_size,
            (flags & HG_CORE_HANDLE_LISTEN) ? NA_SEND : NA_RECV,
            &hg_core_handle->out_buf_plugin_data);
    HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->core_handle.out_buf == NULL,
        error, ret, HG_NOMEM, "Could not allocate buffer for output");

    na_ret = NA_Msg_init_expected(na_class, hg_core_handle->core_handle.out_buf,
        hg_core_handle->core_handle.out_buf_size);
    HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "Could not initialize output buffer (%s)",
        NA_Error_to_string(na_ret));

    /* Create NA operation IDs */
    hg_core_handle->na_send_op_id = NA_Op_create(na_class, NA_OP_SINGLE);
    HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->na_send_op_id == NULL, error,
        ret, HG_NA_ERROR, "Could not create NA op ID");

    hg_core_handle->na_ack_op_id = NA_Op_create(na_class, NA_OP_SINGLE);
    HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->na_ack_op_id == NULL, error, ret,
        HG_NA_ERROR, "Could not create NA op ID");

    hg_atomic_init32(
        &hg_core_handle->op_expected_count, 1); /* Default (no response) */
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) expected_count set to %" PRId32,
        (void *) hg_core_handle, 1);
    hg_atomic_init32(&hg_core_handle->op_completed_count, 0);

    return HG_SUCCESS;

error:
    hg_core_free_na(hg_core_handle);

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_core_free_na(struct hg_core_private_handle *hg_core_handle)
{
    /* Destroy NA op IDs */
    NA_Op_destroy(hg_core_handle->na_class, hg_core_handle->na_send_op_id);
    hg_core_handle->na_send_op_id = NULL;

    NA_Op_destroy(hg_core_handle->na_class, hg_core_handle->na_recv_op_id);
    hg_core_handle->na_recv_op_id = NULL;

    NA_Op_destroy(hg_core_handle->na_class, hg_core_handle->na_ack_op_id);
    hg_core_handle->na_ack_op_id = NULL;

    /* Free buffers */
    if ((hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_MULTI_RECV) &&
        (hg_core_handle->multi_recv_op != NULL)) {
        hg_atomic_decr32(&hg_core_handle->multi_recv_op->ref_count);
        hg_core_handle->multi_recv_op = NULL;
    }
    NA_Msg_buf_free(hg_core_handle->na_class, hg_core_handle->in_buf_storage,
        hg_core_handle->in_buf_plugin_data);
    hg_core_handle->in_buf_storage = NULL;
    hg_core_handle->core_handle.in_buf = NULL;
    hg_core_handle->in_buf_plugin_data = NULL;

    NA_Msg_buf_free(hg_core_handle->na_class,
        hg_core_handle->core_handle.out_buf,
        hg_core_handle->out_buf_plugin_data);
    hg_core_handle->core_handle.out_buf = NULL;
    hg_core_handle->out_buf_plugin_data = NULL;

    if (hg_core_handle->ack_buf != NULL) {
        NA_Msg_buf_free(hg_core_handle->na_class, hg_core_handle->ack_buf,
            hg_core_handle->ack_buf_plugin_data);
        hg_core_handle->ack_buf = NULL;
        hg_core_handle->ack_buf_plugin_data = NULL;
    }

    hg_core_handle->na_class = NULL;
    hg_core_handle->na_context = NULL;
}

/*---------------------------------------------------------------------------*/
static void
hg_core_reset(struct hg_core_private_handle *hg_core_handle)
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_HANDLE_CLASS(hg_core_handle);

    /* TODO context ID must always be reset as it is not passed along with
     * the addr */
    hg_core_handle->core_handle.info.context_id = 0;

    hg_core_handle->request_callback = NULL;
    hg_core_handle->request_arg = NULL;
    hg_core_handle->response_callback = NULL;
    hg_core_handle->response_arg = NULL;
    hg_core_handle->op_type = HG_CORE_PROCESS; /* Default */
    hg_core_handle->tag = 0;
    hg_core_handle->cookie = 0;
    hg_core_handle->ret = HG_SUCCESS;
    hg_core_handle->core_handle.in_buf_used = 0;
    hg_core_handle->core_handle.out_buf_used = 0;
    hg_atomic_init32(
        &hg_core_handle->op_expected_count, 1); /* Default (no response) */
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) expected_count set to %" PRId32,
        (void *) hg_core_handle, 1);
    hg_atomic_init32(&hg_core_handle->op_completed_count, 0);

    /* Free extra data here if needed */
    if (hg_core_class->more_data_cb.release)
        hg_core_class->more_data_cb.release((hg_core_handle_t) hg_core_handle);

    hg_core_header_request_reset(&hg_core_handle->in_header);
    hg_core_header_response_reset(&hg_core_handle->out_header);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_reset_post(struct hg_core_private_handle *hg_core_handle)
{
    struct hg_core_private_context *context =
        HG_CORE_HANDLE_CONTEXT(hg_core_handle);
    struct hg_core_handle_pool *hg_core_handle_pool;
    bool use_multi_recv =
        hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_MULTI_RECV;
    struct hg_core_multi_recv_op *multi_recv_op = hg_core_handle->multi_recv_op;
    hg_return_t ret;

    /* Reset handle info */
    if (hg_core_handle->core_handle.info.addr != HG_CORE_ADDR_NULL) {
        hg_core_addr_free_na((struct hg_core_private_addr *)
                                 hg_core_handle->core_handle.info.addr);
    }
    hg_core_handle->core_handle.info.id = 0;

    /* Reset the handle */
    hg_core_reset(hg_core_handle);

    /* Also reset additional handle parameters */
    hg_atomic_set32(&hg_core_handle->ref_count, 1);
    hg_core_handle->core_handle.rpc_info = NULL;
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count set to %" PRId32,
        (void *) hg_core_handle, 1);

    /* Reset status */
    hg_atomic_set32(&hg_core_handle->status, 0);
    hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) hg_core_handle->ret);

    /* Multi-recv buffers */
    if (use_multi_recv) {
        hg_core_handle->core_handle.in_buf = NULL;
        hg_core_handle->core_handle.in_buf_size = 0;
        hg_core_handle->multi_recv_op = NULL;
    }

#ifdef NA_HAS_SM
    hg_core_handle_pool = (hg_core_handle->na_class ==
                              context->core_context.core_class->na_sm_class)
                              ? context->sm_handle_pool
                              : context->handle_pool;
#else
    hg_core_handle_pool = context->handle_pool;
#endif

    /* Add handle back to pending list */
    hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);
    LIST_INSERT_HEAD(
        &hg_core_handle_pool->pending_list.list, hg_core_handle, pending);
    hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);

    if (use_multi_recv) {
        if (multi_recv_op != NULL &&
            hg_atomic_decr32(&multi_recv_op->ref_count) == 0 &&
            hg_atomic_get32(&multi_recv_op->last)) {
            HG_LOG_SUBSYS_DEBUG(
                ctx, "Reposting multi-recv buffer %d", multi_recv_op->id);

            /* Repost multi recv */
            ret = hg_core_post_multi(multi_recv_op,
                hg_core_handle_pool->na_class, hg_core_handle_pool->na_context);
            HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret,
                "Cannot repost multi-recv operation (%d)", multi_recv_op->id);
            hg_atomic_incr32(&context->multi_recv_op_count);
        }
    } else {
        /* Repost single recv */
        ret = hg_core_post(hg_core_handle);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Cannot post handle");
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_set_rpc(struct hg_core_private_handle *hg_core_handle,
    struct hg_core_private_addr *hg_core_addr, na_addr_t *na_addr, hg_id_t id)
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_HANDLE_CLASS(hg_core_handle);
    struct hg_core_info *info = &hg_core_handle->core_handle.info;
    hg_return_t ret;

    /* We allow for NULL addr to be passed at creation time, this allows
     * for pool of handles to be created and later re-used after a call to
     * HG_Core_reset() */
    if (hg_core_addr && (info->addr != (hg_core_addr_t) hg_core_addr)) {
        if (info->addr != HG_CORE_ADDR_NULL)
            hg_core_addr_free((struct hg_core_private_addr *) info->addr);
        info->addr = (hg_core_addr_t) hg_core_addr;
        hg_atomic_incr32(&hg_core_addr->ref_count);

        /* Set NA addr to use */
        hg_core_handle->na_addr = na_addr;

        /* Set forward call depending on address self */
        if (hg_core_class->init_info.loopback &&
            hg_core_addr->core_addr.is_self) {
            hg_atomic_or32(&hg_core_handle->flags, HG_CORE_SELF_FORWARD);
            hg_core_handle->ops = hg_core_ops_self_g;
        } else {
            hg_atomic_and32(&hg_core_handle->flags, ~HG_CORE_SELF_FORWARD);
            hg_core_handle->ops = hg_core_ops_na_g;
        }
    }

    /* We also allow for NULL RPC id to be passed (same reason as above) */
    if (id && info->id != id) {
        struct hg_core_rpc_info *hg_core_rpc_info;

        /* Retrieve ID function from function map */
        hg_core_rpc_info = hg_core_map_lookup(&hg_core_class->rpc_map, &id);
        HG_CHECK_SUBSYS_ERROR(rpc, hg_core_rpc_info == NULL, error, ret,
            HG_NOENTRY, "Could not find RPC ID (%" PRIu64 ") in RPC map", id);

        info->id = id;

        /* Cache RPC info */
        hg_core_handle->core_handle.rpc_info = hg_core_rpc_info;
        if (hg_core_rpc_info->no_response)
            hg_atomic_or32(&hg_core_handle->flags, HG_CORE_NO_RESPONSE);
        else
            hg_atomic_and32(&hg_core_handle->flags, ~HG_CORE_NO_RESPONSE);
    }
    HG_LOG_SUBSYS_DEBUG(rpc, "Handle (%p) flags set to 0x%x",
        (void *) hg_core_handle, hg_atomic_get32(&hg_core_handle->flags));

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_post(struct hg_core_private_handle *hg_core_handle)
{
    hg_return_t ret;
    na_return_t na_ret;

    /* Post a new unexpected receive */
    na_ret = NA_Msg_recv_unexpected(hg_core_handle->na_class,
        hg_core_handle->na_context, hg_core_recv_input_cb, hg_core_handle,
        hg_core_handle->core_handle.in_buf,
        hg_core_handle->core_handle.in_buf_size,
        hg_core_handle->in_buf_plugin_data, hg_core_handle->na_recv_op_id);
    HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret,
        "Could not post unexpected recv for input buffer (%s)",
        NA_Error_to_string(na_ret));

    HG_LOG_SUBSYS_DEBUG(rpc, "Posted handle (%p)", (void *) hg_core_handle);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_post_multi(struct hg_core_multi_recv_op *multi_recv_op,
    na_class_t *na_class, na_context_t *na_context)
{
    hg_return_t ret;
    na_return_t na_ret;

    hg_atomic_init32(&multi_recv_op->last, 0);
    hg_atomic_init32(&multi_recv_op->ref_count, 0);
    hg_atomic_init32(&multi_recv_op->op_count, 0);

    /* Post a new unexpected receive */
    na_ret = NA_Msg_multi_recv_unexpected(na_class, na_context,
        hg_core_multi_recv_input_cb, multi_recv_op, multi_recv_op->buf,
        multi_recv_op->buf_size, multi_recv_op->plugin_data,
        multi_recv_op->op_id);
    HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "NA_Msg_multi_recv_unexpected() failed (%s)",
        NA_Error_to_string(na_ret));

    HG_LOG_SUBSYS_DEBUG(rpc, "Posted multi-recv buffer (%p, %zu)",
        multi_recv_op->buf, multi_recv_op->buf_size);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_release_input(struct hg_core_private_handle *hg_core_handle)
{
    struct hg_core_private_context *context =
        HG_CORE_HANDLE_CONTEXT(hg_core_handle);
    struct hg_core_handle_pool *hg_core_handle_pool;
    struct hg_core_multi_recv_op *multi_recv_op = hg_core_handle->multi_recv_op;
    hg_return_t ret;

    if (!(hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_MULTI_RECV))
        return HG_SUCCESS;

#ifdef NA_HAS_SM
    hg_core_handle_pool = (hg_core_handle->na_class ==
                              context->core_context.core_class->na_sm_class)
                              ? context->sm_handle_pool
                              : context->handle_pool;
#else
    hg_core_handle_pool = context->handle_pool;
#endif

    /* Multi-recv buffers */
    if (multi_recv_op != NULL) {
        if (!hg_core_handle->multi_recv_copy) {
            hg_core_handle->core_handle.in_buf = NULL;
            hg_core_handle->core_handle.in_buf_size = 0;
        }
        hg_core_handle->multi_recv_op = NULL;

        if (hg_atomic_decr32(&multi_recv_op->ref_count) == 0 &&
            hg_atomic_get32(&multi_recv_op->last)) {
            HG_LOG_SUBSYS_DEBUG(
                ctx, "Reposting multi-recv buffer %d", multi_recv_op->id);

            /* Repost multi recv */
            ret = hg_core_post_multi(multi_recv_op,
                hg_core_handle_pool->na_class, hg_core_handle_pool->na_context);
            HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret,
                "Cannot repost multi-recv operation (%d)", multi_recv_op->id);
            hg_atomic_incr32(&context->multi_recv_op_count);
        }
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_forward(struct hg_core_private_handle *hg_core_handle,
    hg_core_cb_t callback, void *arg, uint8_t flags, hg_size_t payload_size)
{
    int32_t HG_DEBUG_LOG_USED ref_count;
    int32_t status;
    hg_size_t header_size;
    hg_return_t ret = HG_SUCCESS;

    status = hg_atomic_get32(&hg_core_handle->status);
    HG_CHECK_SUBSYS_ERROR(rpc,
        !(status & HG_CORE_OP_COMPLETED) || (status & HG_CORE_OP_QUEUED), done,
        ret, HG_BUSY, "Attempting to use handle that was not completed");

    /* Increment ref_count on handle to allow for destroy to be
     * pre-emptively called */
    ref_count = hg_atomic_incr32(&hg_core_handle->ref_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count incr to %" PRId32,
        (void *) hg_core_handle, ref_count);

    /* Reset op counts */
    hg_atomic_set32(
        &hg_core_handle->op_expected_count, 1); /* Default (no response) */
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) expected_count set to %" PRId32,
        (void *) hg_core_handle, 1);
    hg_atomic_set32(&hg_core_handle->op_completed_count, 0);

    /* Reset handle ret */
    hg_core_handle->ret = HG_SUCCESS;

    /* Reset status */
    hg_atomic_set32(&hg_core_handle->status, 0);
    hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) hg_core_handle->ret);

    /* Set header size */
    header_size = hg_core_header_request_get_size() +
                  hg_core_handle->core_handle.na_in_header_offset;

    /* Set the actual size of the msg that needs to be transmitted */
    hg_core_handle->core_handle.in_buf_used = header_size + payload_size;
    HG_CHECK_SUBSYS_ERROR(rpc,
        hg_core_handle->core_handle.in_buf_used >
            hg_core_handle->core_handle.in_buf_size,
        error, ret, HG_MSGSIZE, "Exceeding input buffer size");

    /* Parse flags */
    if (flags & HG_CORE_MORE_DATA)
        hg_atomic_or32(&hg_core_handle->flags, HG_CORE_MORE_DATA);
    else
        hg_atomic_and32(&hg_core_handle->flags, ~HG_CORE_MORE_DATA);

    /* Set callback, keep request and response callbacks separate so that
     * they do not get overwritten when forwarding to ourself */
    hg_core_handle->request_callback = callback;
    hg_core_handle->request_arg = arg;

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    /* Increment counter */
    hg_atomic_incr64(
        HG_CORE_HANDLE_CLASS(hg_core_handle)->counters.rpc_req_sent_count);
#endif

    /* If addr is self, forward locally, otherwise send the encoded buffer
     * through NA and pre-post response */
    ret = hg_core_handle->ops.forward(hg_core_handle);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not forward buffer");

done:
    return ret;

error:
    /* Handle is no longer in use */
    hg_atomic_set32(&hg_core_handle->status, HG_CORE_OP_COMPLETED);

    /* Rollback ref_count taken above */
    ref_count = hg_atomic_decr32(&hg_core_handle->ref_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count decr to %" PRId32,
        (void *) hg_core_handle, ref_count);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_forward_self(struct hg_core_private_handle *hg_core_handle)
{
    hg_return_t ret;

    /* Save ref_count, when sending to self and no response is being sent,
     * we can only use that refcount to determine that the RPC callback has
     * been fully executed. */
    if (hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_NO_RESPONSE)
        hg_atomic_set32(&hg_core_handle->no_response_done,
            hg_atomic_get32(&hg_core_handle->ref_count));

    /* Set operation type for trigger */
    hg_core_handle->op_type = HG_CORE_PROCESS;

    /* Process input */
    ret = hg_core_process_input(hg_core_handle);
    if (ret != HG_SUCCESS) {
        HG_LOG_SUBSYS_ERROR(rpc, "Could not process input");
        hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) ret);
    }

    /* Mark as completed */
    hg_core_complete_op(hg_core_handle);

    /* Always handle error from callback when forwarding to self, implies
     * submission is always successful. */
    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_forward_na(struct hg_core_private_handle *hg_core_handle)
{
    hg_return_t ret;
    na_return_t na_ret;

    /* Set operation type for trigger */
    hg_core_handle->op_type = HG_CORE_FORWARD;

    /* Set header */
    hg_core_handle->in_header.msg.request.id =
        hg_core_handle->core_handle.info.id;
    hg_core_handle->in_header.msg.request.flags =
        (uint8_t) (hg_atomic_get32(&hg_core_handle->flags) & 0xff);
    /* Set the cookie as origin context ID, so that when the cookie is
     * unpacked by the target and assigned to HG info context_id, the NA
     * layer knows which context ID it needs to send the response to. */
    hg_core_handle->in_header.msg.request.cookie =
        hg_core_handle->core_handle.info.context->id;

    /* Encode request header */
    ret = hg_core_proc_header_request(
        &hg_core_handle->core_handle, &hg_core_handle->in_header, HG_ENCODE);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not encode header");

    /* Generate tag */
    hg_core_handle->tag =
        hg_core_gen_request_tag(HG_CORE_HANDLE_CLASS(hg_core_handle));

    /* Pre-post recv (output) if response is expected */
    if (!(hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_NO_RESPONSE)) {
        int32_t HG_DEBUG_LOG_USED expected_count;

        na_ret = NA_Msg_recv_expected(hg_core_handle->na_class,
            hg_core_handle->na_context, hg_core_recv_output_cb, hg_core_handle,
            hg_core_handle->core_handle.out_buf,
            hg_core_handle->core_handle.out_buf_size,
            hg_core_handle->out_buf_plugin_data, hg_core_handle->na_addr,
            hg_core_handle->core_handle.info.context_id, hg_core_handle->tag,
            hg_core_handle->na_recv_op_id);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not post recv for output buffer (%s)",
            NA_Error_to_string(na_ret));

        /* Increment number of expected completions */
        expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
        HG_LOG_SUBSYS_DEBUG(rpc_ref,
            "Handle (%p) expected_count incr to %" PRId32,
            (void *) hg_core_handle, expected_count);
    }

    /* Mark handle as posted */
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_POSTED);

    /* Post send (input) */
    na_ret = NA_Msg_send_unexpected(hg_core_handle->na_class,
        hg_core_handle->na_context, hg_core_send_input_cb, hg_core_handle,
        hg_core_handle->core_handle.in_buf,
        hg_core_handle->core_handle.in_buf_used,
        hg_core_handle->in_buf_plugin_data, hg_core_handle->na_addr,
        hg_core_handle->core_handle.info.context_id, hg_core_handle->tag,
        hg_core_handle->na_send_op_id);
    HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error_send, ret,
        (hg_return_t) na_ret, "Could not post send for input buffer (%s)",
        NA_Error_to_string(na_ret));

    return HG_SUCCESS;

error:
    return ret;

error_send:
    hg_atomic_and32(&hg_core_handle->status, ~HG_CORE_OP_POSTED);
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);

    if (hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_NO_RESPONSE) {
        /* No recv was posted */
        return ret;
    } else {
        int32_t HG_DEBUG_LOG_USED expected_count =
            hg_atomic_decr32(&hg_core_handle->op_expected_count);
        HG_LOG_SUBSYS_DEBUG(rpc_ref,
            "Handle (%p) expected_count decr to %" PRId32,
            (void *) hg_core_handle, expected_count);

        /* Keep error for return status */
        hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) ret);

        /* Mark op as canceled and let it complete */
        hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_CANCELED);

        /* Cancel the above posted recv op */
        na_ret = NA_Cancel(hg_core_handle->na_class, hg_core_handle->na_context,
            hg_core_handle->na_recv_op_id);
        HG_CHECK_SUBSYS_ERROR_DONE(rpc, na_ret != NA_SUCCESS,
            "Could not cancel recv op id (%s)", NA_Error_to_string(na_ret));

        /* Return success here but callback will return canceled */
        return HG_SUCCESS;
    }
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_respond(struct hg_core_private_handle *hg_core_handle,
    hg_core_cb_t callback, void *arg, uint8_t flags, hg_size_t payload_size,
    hg_return_t ret_code)
{
    int32_t HG_DEBUG_LOG_USED ref_count;
    hg_size_t header_size;
    hg_return_t ret = HG_SUCCESS;

    /* Cannot respond if no_response flag set */
    HG_CHECK_SUBSYS_ERROR(rpc,
        hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_NO_RESPONSE, done,
        ret, HG_OPNOTSUPPORTED, "Sending response was disabled on that RPC");

    /* Reset handle ret */
    hg_core_handle->ret = HG_SUCCESS;

    /* Reset status */
    hg_atomic_and32(&hg_core_handle->status, ~HG_CORE_OP_COMPLETED);
    hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) hg_core_handle->ret);

    /* Set header size */
    header_size = hg_core_header_response_get_size() +
                  hg_core_handle->core_handle.na_out_header_offset;

    /* Set the actual size of the msg that needs to be transmitted */
    hg_core_handle->core_handle.out_buf_used = header_size + payload_size;
    HG_CHECK_SUBSYS_ERROR(rpc,
        hg_core_handle->core_handle.out_buf_used >
            hg_core_handle->core_handle.out_buf_size,
        error, ret, HG_MSGSIZE, "Exceeding output buffer size");

    /* Parse flags */
    if (flags & HG_CORE_MORE_DATA)
        hg_atomic_or32(&hg_core_handle->flags, HG_CORE_MORE_DATA);
    else
        hg_atomic_and32(&hg_core_handle->flags, ~HG_CORE_MORE_DATA);

    /* Set callback, keep request and response callbacks separate so that
     * they do not get overwritten when forwarding to ourself */
    hg_core_handle->response_callback = callback;
    hg_core_handle->response_arg = arg;

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    /* Increment counter */
    hg_atomic_incr64(
        HG_CORE_HANDLE_CLASS(hg_core_handle)->counters.rpc_resp_sent_count);
#endif

    /* If addr is self, forward locally, otherwise send the encoded buffer
     * through NA and pre-post response */
    ret = hg_core_handle->ops.respond(hg_core_handle, ret_code);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not respond");

done:
    return ret;

error:
    /* Handle is no longer in use */
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_COMPLETED);

    /* Decrement refcount on handle */
    ref_count = hg_atomic_decr32(&hg_core_handle->ref_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count decr to %" PRId32,
        (void *) hg_core_handle, ref_count);

    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
hg_core_respond_self(
    struct hg_core_private_handle *hg_core_handle, hg_return_t ret_code)
{
    int32_t HG_DEBUG_LOG_USED expected_count;

    /* Set operation type for trigger */
    hg_core_handle->op_type = HG_CORE_RESPOND;

    /* Pass return code */
    hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) ret_code);

    /* Increment number of expected completions */
    expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) expected_count incr to %" PRId32,
        (void *) hg_core_handle, expected_count);

    /* Complete and add to completion queue */
    hg_core_complete_op(hg_core_handle);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_no_respond_self(struct hg_core_private_handle *hg_core_handle)
{
    int32_t HG_DEBUG_LOG_USED expected_count;

    /* Set operation type for trigger */
    hg_core_handle->op_type = HG_CORE_FORWARD;

    /* Reset saved refcount */
    hg_atomic_set32(&hg_core_handle->no_response_done, 0);

    /* Increment number of expected completions */
    expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) expected_count incr to %" PRId32,
        (void *) hg_core_handle, expected_count);

    /* Complete and add to completion queue */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_respond_na(
    struct hg_core_private_handle *hg_core_handle, hg_return_t ret_code)
{
    int32_t HG_DEBUG_LOG_USED expected_count;
    hg_return_t ret;
    na_return_t na_ret;
    bool ack_recv_posted = false;

    /* Set header */
    hg_core_handle->out_header.msg.response.ret_code = (int8_t) ret_code;
    hg_core_handle->out_header.msg.response.flags =
        (uint8_t) (hg_atomic_get32(&hg_core_handle->flags) & 0xff);
    hg_core_handle->out_header.msg.response.cookie = hg_core_handle->cookie;

    /* Encode response header */
    ret = hg_core_proc_header_response(
        &hg_core_handle->core_handle, &hg_core_handle->out_header, HG_ENCODE);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not encode header");

    /* Increment number of expected completions */
    expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) expected_count incr to %" PRId32,
        (void *) hg_core_handle, expected_count);

    /* Set operation type for trigger */
    hg_core_handle->op_type = HG_CORE_RESPOND;

    /* More data on output requires an ack once it is processed */
    if (hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_MORE_DATA) {
        size_t buf_size =
            hg_core_handle->core_handle.na_out_header_offset + sizeof(uint8_t);

        HG_LOG_SUBSYS_WARNING(perf,
            "Allocating %zu byte(s) to send extra output data for handle %p",
            buf_size, (void *) hg_core_handle);

        /* Keep the buffer allocated if we are prone to using ack buffers */
        if (hg_core_handle->ack_buf == NULL) {
            hg_core_handle->ack_buf = NA_Msg_buf_alloc(hg_core_handle->na_class,
                buf_size, NA_RECV, &hg_core_handle->ack_buf_plugin_data);
            HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->ack_buf == NULL, error,
                ret, HG_NA_ERROR, "Could not allocate buffer for ack");

            na_ret = NA_Msg_init_expected(
                hg_core_handle->na_class, hg_core_handle->ack_buf, buf_size);
            HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
                (hg_return_t) na_ret, "Could not initialize ack buffer (%s)",
                NA_Error_to_string(na_ret));
        }

        /* Increment number of expected completions */
        expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
        HG_LOG_SUBSYS_DEBUG(rpc_ref,
            "Handle (%p) expected_count incr to %" PRId32,
            (void *) hg_core_handle, expected_count);

        /* Pre-post recv (ack) if more data is expected */
        na_ret = NA_Msg_recv_expected(hg_core_handle->na_class,
            hg_core_handle->na_context, hg_core_ack_cb, hg_core_handle,
            hg_core_handle->ack_buf, buf_size,
            hg_core_handle->ack_buf_plugin_data, hg_core_handle->na_addr,
            hg_core_handle->core_handle.info.context_id, hg_core_handle->tag,
            hg_core_handle->na_ack_op_id);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not post recv for ack buffer (%s)",
            NA_Error_to_string(na_ret));
        ack_recv_posted = true;
    }

    /* Mark handle as posted */
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_POSTED);

    /* Post expected send (output) */
    na_ret = NA_Msg_send_expected(hg_core_handle->na_class,
        hg_core_handle->na_context, hg_core_send_output_cb, hg_core_handle,
        hg_core_handle->core_handle.out_buf,
        hg_core_handle->core_handle.out_buf_used,
        hg_core_handle->out_buf_plugin_data, hg_core_handle->na_addr,
        hg_core_handle->core_handle.info.context_id, hg_core_handle->tag,
        hg_core_handle->na_send_op_id);
    /* Expected sends should always succeed after retry */
    HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "Could not post send for output buffer (%s)",
        NA_Error_to_string(na_ret));

    return HG_SUCCESS;

error:
    hg_atomic_and32(&hg_core_handle->status, ~HG_CORE_OP_POSTED);
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);

    if (ack_recv_posted) {
        expected_count = hg_atomic_decr32(&hg_core_handle->op_expected_count);
        HG_LOG_SUBSYS_DEBUG(rpc_ref,
            "Handle (%p) expected_count decr to %" PRId32,
            (void *) hg_core_handle, expected_count);

        /* Keep error for return status */
        hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) ret);

        /* Mark op as canceled and let it complete */
        hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_CANCELED);

        /* Cancel the above posted recv ack op */
        na_ret = NA_Cancel(hg_core_handle->na_class, hg_core_handle->na_context,
            hg_core_handle->na_ack_op_id);
        HG_CHECK_SUBSYS_ERROR_DONE(rpc, na_ret != NA_SUCCESS,
            "Could not cancel ack op id (%s)", NA_Error_to_string(na_ret));

        /* Return success here but callback will return canceled */
        return HG_SUCCESS;
    } else if (hg_core_handle->ack_buf != NULL) {
        NA_Msg_buf_free(hg_core_handle->na_class, hg_core_handle->ack_buf,
            hg_core_handle->ack_buf_plugin_data);
        hg_core_handle->ack_buf = NULL;
        hg_core_handle->ack_buf_plugin_data = NULL;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_send_input_cb(const struct na_cb_info *callback_info)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) callback_info->arg;

    if (callback_info->ret == NA_SUCCESS) {
        /* Nothing */
    } else if (callback_info->ret == NA_CANCELED) {
        HG_CHECK_SUBSYS_WARNING(rpc,
            hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_COMPLETED,
            "Operation was completed");
        HG_LOG_SUBSYS_DEBUG(
            rpc, "NA_CANCELED event on handle %p", (void *) hg_core_handle);

        hg_atomic_cas32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS,
            (int32_t) HG_CANCELED);
    } else { /* All other errors */
        int32_t status;

        /* Mark handle as errored */
        status = hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);

        /* Keep first non-success ret status */
        hg_atomic_cas32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS,
            (int32_t) callback_info->ret);
        HG_LOG_SUBSYS_ERROR(rpc, "NA callback returned error (%s)",
            NA_Error_to_string(callback_info->ret));

        if (!(status & HG_CORE_OP_CANCELED) &&
            !(hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_NO_RESPONSE)) {
            na_return_t na_ret;

            hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_CANCELED);

            /* Cancel posted recv for response */
            na_ret = NA_Cancel(hg_core_handle->na_class,
                hg_core_handle->na_context, hg_core_handle->na_recv_op_id);
            HG_CHECK_SUBSYS_ERROR_DONE(rpc, na_ret != NA_SUCCESS,
                "Could not cancel recv op id (%s)", NA_Error_to_string(na_ret));
        }
    }

    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static void
hg_core_recv_input_cb(const struct na_cb_info *callback_info)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) callback_info->arg;
    struct hg_core_private_context *context =
        HG_CORE_HANDLE_CONTEXT(hg_core_handle);
    struct hg_core_handle_pool *hg_core_handle_pool;
    const struct na_cb_info_recv_unexpected *na_cb_info_recv_unexpected =
        &callback_info->info.recv_unexpected;
    hg_return_t ret;

/* Remove handle from pending list */
#ifdef NA_HAS_SM
    hg_core_handle_pool = (hg_core_handle->na_class ==
                              context->core_context.core_class->na_sm_class)
                              ? context->sm_handle_pool
                              : context->handle_pool;
#else
    hg_core_handle_pool = context->handle_pool;
#endif
    hg_thread_spin_lock(&hg_core_handle_pool->pending_list.lock);
    LIST_REMOVE(hg_core_handle, pending);
    hg_thread_spin_unlock(&hg_core_handle_pool->pending_list.lock);
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    /* Increment counter */
    hg_atomic_incr64(HG_CORE_HANDLE_CLASS(hg_core_handle)
                         ->counters.rpc_req_recv_active_count);
    hg_core_handle->active = true;
#endif

    if (callback_info->ret == NA_SUCCESS) {
        /* Extend pool if all handles are being utilized */
        if (hg_core_handle_pool->incr_count > 0 &&
            !hg_atomic_get32(&context->unposting) &&
            hg_core_handle_pool_empty(hg_core_handle_pool)) {
            HG_LOG_SUBSYS_WARNING(perf,
                "Pre-posted handles have all been consumed / are being "
                "utilized, posting %u more",
                hg_core_handle_pool->incr_count);

            ret = hg_core_handle_pool_extend(hg_core_handle_pool);
            HG_CHECK_SUBSYS_HG_ERROR(
                rpc, error, ret, "Could not extend handle pool");
        }

        /* Fill unexpected info */
        hg_core_handle->na_addr = na_cb_info_recv_unexpected->source;
#ifdef NA_HAS_SM
        if (hg_core_handle->na_class ==
            hg_core_handle->core_handle.info.core_class->na_sm_class) {
            HG_LOG_SUBSYS_DEBUG(rpc, "Using NA SM class for this handle");
            hg_core_handle->core_handle.info.addr->na_sm_addr =
                hg_core_handle->na_addr;
        } else
#endif
            hg_core_handle->core_handle.info.addr->na_addr =
                hg_core_handle->na_addr;
        hg_core_handle->tag = na_cb_info_recv_unexpected->tag;
        hg_core_handle->core_handle.in_buf_used =
            na_cb_info_recv_unexpected->actual_buf_size;
        HG_CHECK_SUBSYS_ERROR(rpc,
            hg_core_handle->core_handle.in_buf_used >
                hg_core_handle->core_handle.in_buf_size,
            error, ret, HG_OVERFLOW,
            "Actual transfer size (%zu) is too large for unexpected recv",
            hg_core_handle->core_handle.in_buf_used);

        HG_LOG_SUBSYS_DEBUG(rpc,
            "Processing input for handle %p, tag=%u, buf_size=%zu",
            (void *) hg_core_handle, hg_core_handle->tag,
            hg_core_handle->core_handle.in_buf_used);

        /* Process input information */
        ret = hg_core_process_input(hg_core_handle);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not process input");

        /* Complete operation */
        hg_core_complete_op(hg_core_handle);
    } else if (callback_info->ret == NA_CANCELED) {
        HG_CHECK_SUBSYS_WARNING(rpc,
            hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_COMPLETED,
            "Operation was completed");
        HG_LOG_SUBSYS_DEBUG(
            rpc, "NA_CANCELED event on handle %p", (void *) hg_core_handle);

        /* Prevent re-initialization */
        hg_core_handle->reuse = false;

        /* Clean up handle */
        (void) hg_core_destroy(hg_core_handle);
    } else {
        HG_LOG_SUBSYS_ERROR(rpc, "NA callback returned error (%s)",
            NA_Error_to_string(callback_info->ret));

        /* Prevent re-initialization */
        hg_core_handle->reuse = false;

        /* Clean up handle */
        (void) hg_core_destroy(hg_core_handle);
    }

    return;

error:
    /* Mark handle as errored */
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);
    hg_atomic_cas32(
        &hg_core_handle->ret_status, (int32_t) HG_SUCCESS, (int32_t) ret);

    /* Complete operation */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static void
hg_core_multi_recv_input_cb(const struct na_cb_info *callback_info)
{
    struct hg_core_multi_recv_op *multi_recv_op =
        (struct hg_core_multi_recv_op *) callback_info->arg;
    struct hg_core_private_context *context = multi_recv_op->context;
    const struct na_cb_info_multi_recv_unexpected
        *na_cb_info_multi_recv_unexpected =
            &callback_info->info.multi_recv_unexpected;
    struct hg_core_private_handle *hg_core_handle = NULL;
    hg_return_t ret;

    if (callback_info->ret == NA_SUCCESS) {
        /* Get a new handle from the pool */
        ret = hg_core_handle_pool_get(context->handle_pool, &hg_core_handle);
        HG_CHECK_SUBSYS_HG_ERROR(
            rpc, error, ret, "Could not get handle from pool");
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
        /* Increment counter */
        hg_atomic_incr64(HG_CORE_HANDLE_CLASS(hg_core_handle)
                             ->counters.rpc_req_recv_active_count);
        hg_core_handle->active = true;
#endif
        hg_core_handle->multi_recv_op = multi_recv_op;
        hg_atomic_incr32(&multi_recv_op->op_count);
        hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_MULTI_RECV);
        /* Prevent from reposting multi-recv buffer until done with handle */
        hg_atomic_incr32(&multi_recv_op->ref_count);
        hg_core_handle->multi_recv_copy =
            (unsigned int) hg_atomic_get32(&context->multi_recv_op_count) <=
            HG_CORE_CONTEXT_CLASS(context)->init_info.multi_recv_copy_threshold;

        if (na_cb_info_multi_recv_unexpected->last) {
            HG_LOG_SUBSYS_DEBUG(rpc,
                "Multi-recv buffer %d has been consumed (%" PRId32
                " operations completed)",
                multi_recv_op->id, hg_atomic_get32(&multi_recv_op->op_count));
            hg_atomic_set32(&multi_recv_op->last, true);
            if (hg_atomic_decr32(&context->multi_recv_op_count) == 0) {
                unsigned int multi_recv_op_max =
                    HG_CORE_CONTEXT_CLASS(context)->init_info.multi_recv_op_max;
                unsigned int i;
                HG_LOG_SUBSYS_WARNING(ctx,
                    "All multi-recv buffers have been consumed, consider "
                    "increasing request_post_init init info in order to "
                    "increase initial buffer sizes");
                for (i = 0; i < multi_recv_op_max; i++)
                    HG_LOG_SUBSYS_WARNING(ctx,
                        "Multi-recv buffer %d held by %d handles", i,
                        hg_atomic_get32(&context->multi_recv_ops[i].ref_count));
            }
        }

        /* Fill unexpected info */
        hg_core_handle->na_addr = na_cb_info_multi_recv_unexpected->source;
        hg_core_handle->core_handle.info.addr->na_addr =
            hg_core_handle->na_addr;
        hg_core_handle->tag = na_cb_info_multi_recv_unexpected->tag;
        hg_core_handle->core_handle.in_buf_used =
            na_cb_info_multi_recv_unexpected->actual_buf_size;

        /* Either copy the buffer to release early or point to the actual
         * multi-recv buffer space to save a memcpy */
        if (hg_core_handle->multi_recv_copy) {
            HG_CHECK_SUBSYS_ERROR(rpc,
                hg_core_handle->core_handle.in_buf_used >
                    hg_core_handle->in_buf_storage_size,
                error, ret, HG_OVERFLOW,
                "Actual transfer size (%zu) is too large for unexpected recv",
                hg_core_handle->core_handle.in_buf_used);
            HG_LOG_SUBSYS_DEBUG(rpc,
                "Copying multi-recv payload of size %zu for handle (%p)",
                hg_core_handle->core_handle.in_buf_used,
                (void *) hg_core_handle);
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
            /* Increment counter */
            hg_atomic_incr64(HG_CORE_CONTEXT_CLASS(context)
                                 ->counters.rpc_multi_recv_copy_count);
#endif

            memcpy(hg_core_handle->in_buf_storage,
                na_cb_info_multi_recv_unexpected->actual_buf,
                hg_core_handle->core_handle.in_buf_used);
            hg_core_handle->core_handle.in_buf_size =
                hg_core_handle->in_buf_storage_size;
            hg_core_handle->core_handle.in_buf = hg_core_handle->in_buf_storage;

            ret = hg_core_release_input(hg_core_handle);
            HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret,
                "Could not release input for handle (%p)",
                (void *) hg_core_handle);
        } else {
            HG_LOG_SUBSYS_DEBUG(rpc,
                "Using direct multi-recv payload of size %zu for handle (%p)",
                hg_core_handle->core_handle.in_buf_used,
                (void *) hg_core_handle);
            hg_core_handle->core_handle.in_buf_size =
                hg_core_handle->core_handle.in_buf_used;
            hg_core_handle->core_handle.in_buf =
                na_cb_info_multi_recv_unexpected->actual_buf;
        }

        HG_LOG_SUBSYS_DEBUG(rpc,
            "Processing input for handle %p, tag=%u, buf_size=%zu",
            (void *) hg_core_handle, hg_core_handle->tag,
            hg_core_handle->core_handle.in_buf_used);

        /* Process input information */
        ret = hg_core_process_input(hg_core_handle);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not process input");

        /* Complete operation */
        hg_core_complete_op(hg_core_handle);
    } else if (callback_info->ret == NA_CANCELED) {
        HG_LOG_SUBSYS_DEBUG(
            rpc, "NA_CANCELED event on multi-recv op %d", multi_recv_op->id);
        hg_atomic_decr32(&context->multi_recv_op_count);
    } else {
        HG_LOG_SUBSYS_ERROR(rpc, "NA callback returned error (%s)",
            NA_Error_to_string(callback_info->ret));
        hg_atomic_decr32(&context->multi_recv_op_count);
        /* TODO can an unexpected multi-recv operation ever fail? */
    }

    return;

error:
    if (hg_core_handle == NULL)
        return;

    /* Mark handle as errored */
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);
    hg_atomic_cas32(
        &hg_core_handle->ret_status, (int32_t) HG_SUCCESS, (int32_t) ret);

    /* Complete operation */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_process_input(struct hg_core_private_handle *hg_core_handle)
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_HANDLE_CLASS(hg_core_handle);
    hg_return_t ret;

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    /* Increment counter */
    hg_atomic_incr64(hg_core_class->counters.rpc_req_recv_count);
#endif

    /* We can skip RPC headers etc if we are sending to ourselves */
    if (!(hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_SELF_FORWARD)) {
        /* Get and verify input header */
        ret = hg_core_proc_header_request(&hg_core_handle->core_handle,
            &hg_core_handle->in_header, HG_DECODE);
        HG_CHECK_SUBSYS_HG_ERROR(
            rpc, error, ret, "Could not decode request header");

        /* Get operation ID from header */
        hg_core_handle->core_handle.info.id =
            hg_core_handle->in_header.msg.request.id;
        hg_core_handle->cookie = hg_core_handle->in_header.msg.request.cookie;
        /* TODO assign target ID from cookie directly for now */
        hg_core_handle->core_handle.info.context_id = hg_core_handle->cookie;

        /* Parse flags */
        hg_atomic_set32(&hg_core_handle->flags,
            hg_core_handle->in_header.msg.request.flags);
    }

    HG_LOG_SUBSYS_DEBUG(rpc,
        "Processed input for handle %p, ID=%" PRIu64 ", cookie=%" PRIu8
        ", no_response=%d",
        (void *) hg_core_handle, hg_core_handle->core_handle.info.id,
        hg_core_handle->cookie,
        hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_NO_RESPONSE);

    /* Must let upper layer get extra payload if HG_CORE_MORE_DATA is set */
    if (hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_MORE_DATA) {
        int32_t HG_DEBUG_LOG_USED expected_count;

        HG_CHECK_SUBSYS_ERROR(rpc, hg_core_class->more_data_cb.acquire == NULL,
            error, ret, HG_OPNOTSUPPORTED,
            "No callback defined for acquiring more data");

        HG_LOG_SUBSYS_WARNING(perf,
            "Must recv extra input data payload for handle %p",
            (void *) hg_core_handle);

        /* Increment number of expected completions */
        expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
        HG_LOG_SUBSYS_DEBUG(rpc_ref,
            "Handle (%p) expected_count incr to %" PRId32,
            (void *) hg_core_handle, expected_count);

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
        /* Increment counter */
        hg_atomic_incr64(hg_core_class->counters.rpc_req_extra_count);
#endif

        ret = hg_core_class->more_data_cb.acquire(
            (hg_core_handle_t) hg_core_handle, HG_INPUT,
            hg_core_more_data_complete);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret,
            "Error in HG core handle more data acquire callback for handle %p",
            (void *) hg_core_handle);
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_send_output_cb(const struct na_cb_info *callback_info)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) callback_info->arg;

    if (callback_info->ret == NA_SUCCESS) {
        /* Nothing */
    } else if (callback_info->ret == NA_CANCELED) {
        HG_CHECK_SUBSYS_WARNING(rpc,
            hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_COMPLETED,
            "Operation was completed");
        HG_LOG_SUBSYS_DEBUG(
            rpc, "NA_CANCELED event on handle %p", (void *) hg_core_handle);

        hg_atomic_cas32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS,
            (int32_t) HG_CANCELED);
    } else {
        /* Mark handle as errored */
        hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);

        /* Keep first non-success ret status */
        hg_atomic_cas32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS,
            (int32_t) callback_info->ret);
        HG_LOG_SUBSYS_ERROR(rpc, "NA callback returned error (%s)",
            NA_Error_to_string(callback_info->ret));
    }

    /* Complete operation */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_recv_output_cb(const struct na_cb_info *callback_info)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) callback_info->arg;
    hg_return_t ret;

    if (callback_info->ret == NA_SUCCESS) {
        hg_core_handle->core_handle.out_buf_used =
            callback_info->info.recv_expected.actual_buf_size;
        HG_CHECK_SUBSYS_ERROR(rpc,
            hg_core_handle->core_handle.out_buf_used >
                hg_core_handle->core_handle.out_buf_size,
            error, ret, HG_OVERFLOW,
            "Actual transfer size (%zu) is too large for expected recv",
            hg_core_handle->core_handle.out_buf_used);

        HG_LOG_SUBSYS_DEBUG(rpc,
            "Processing output for handle %p, tag=%u, buf_size=%zu",
            (void *) hg_core_handle, hg_core_handle->tag,
            hg_core_handle->core_handle.out_buf_used);

        /* Process output information */
        ret = hg_core_process_output(hg_core_handle, hg_core_send_ack);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not process output");

    } else if (callback_info->ret == NA_CANCELED) {
        HG_CHECK_SUBSYS_WARNING(rpc,
            hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_COMPLETED,
            "Operation was completed");
        HG_LOG_SUBSYS_DEBUG(
            rpc, "NA_CANCELED event on handle %p", (void *) hg_core_handle);

        hg_atomic_cas32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS,
            (int32_t) HG_CANCELED);
    } else
        HG_GOTO_SUBSYS_ERROR(rpc, error, ret, (hg_return_t) callback_info->ret,
            "NA callback returned error (%s)",
            NA_Error_to_string(callback_info->ret));

    /* Complete operation */
    hg_core_complete_op(hg_core_handle);

    return;

error:
    /* Mark handle as errored */
    hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);
    hg_atomic_cas32(
        &hg_core_handle->ret_status, (int32_t) HG_SUCCESS, (int32_t) ret);

    /* Complete operation */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_process_output(struct hg_core_private_handle *hg_core_handle,
    void (*done_callback)(hg_core_handle_t, hg_return_t))
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_HANDLE_CLASS(hg_core_handle);
    hg_return_t ret;

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    /* Increment counter */
    hg_atomic_incr64(hg_core_class->counters.rpc_resp_recv_count);
#endif

    if (!(hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_SELF_FORWARD)) {
        /* Get and verify output header */
        ret = hg_core_proc_header_response(&hg_core_handle->core_handle,
            &hg_core_handle->out_header, HG_DECODE);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Could not decode header");

        /* Get return code from header */
        hg_atomic_set32(&hg_core_handle->ret_status,
            (int32_t) hg_core_handle->out_header.msg.response.ret_code);

        /* Parse flags */
        hg_atomic_set32(&hg_core_handle->flags,
            hg_core_handle->out_header.msg.response.flags);
    }

    HG_LOG_SUBSYS_DEBUG(rpc,
        "Processed output for handle %p, ID=%" PRIu64 ", ret=%" PRId32,
        (void *) hg_core_handle, hg_core_handle->core_handle.info.id,
        hg_atomic_get32(&hg_core_handle->ret_status));

    /* Must let upper layer get extra payload if HG_CORE_MORE_DATA is set */
    if (hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_MORE_DATA) {
        int32_t HG_DEBUG_LOG_USED expected_count;

        HG_CHECK_SUBSYS_ERROR(rpc, hg_core_class->more_data_cb.acquire == NULL,
            error, ret, HG_OPNOTSUPPORTED,
            "No callback defined for acquiring more data");

        HG_LOG_SUBSYS_WARNING(perf,
            "Must recv extra output data payload for handle %p",
            (void *) hg_core_handle);

        /* Increment number of expected completions */
        expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
        HG_LOG_SUBSYS_DEBUG(rpc_ref,
            "Handle (%p) expected_count incr to %" PRId32,
            (void *) hg_core_handle, expected_count);

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
        /* Increment counter */
        hg_atomic_incr64(hg_core_class->counters.rpc_resp_extra_count);
#endif

        ret = hg_core_class->more_data_cb.acquire(
            (hg_core_handle_t) hg_core_handle, HG_OUTPUT, done_callback);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret,
            "Error in HG core handle more data acquire callback for handle %p",
            (void *) hg_core_handle);
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_more_data_complete(hg_core_handle_t handle, hg_return_t ret)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) handle;

    if (ret != HG_SUCCESS) {
        if (ret != HG_CANCELED) {
            /* Mark handle as errored */
            hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);
        }
        hg_atomic_cas32(
            &hg_core_handle->ret_status, (int32_t) HG_SUCCESS, (int32_t) ret);
    }

    /* Complete and add to completion queue */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static void
hg_core_send_ack(hg_core_handle_t handle, hg_return_t ret)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) handle;
    na_return_t na_ret;
    size_t buf_size = handle->na_out_header_offset + sizeof(uint8_t);

    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret, "Aborting ack send");

    /* Keep the buffer allocated if we are prone to using ack buffers */
    if (hg_core_handle->ack_buf == NULL) {
        /* Allocate buffer for ack */
        hg_core_handle->ack_buf = NA_Msg_buf_alloc(hg_core_handle->na_class,
            buf_size, NA_SEND, &hg_core_handle->ack_buf_plugin_data);
        HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle->ack_buf == NULL, error, ret,
            HG_NOMEM, "Could not allocate buffer for ack");

        na_ret = NA_Msg_init_expected(
            hg_core_handle->na_class, hg_core_handle->ack_buf, buf_size);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not initialize ack buffer (%s)",
            NA_Error_to_string(na_ret));
    }

    /* Post expected send (ack) */
    na_ret = NA_Msg_send_expected(hg_core_handle->na_class,
        hg_core_handle->na_context, hg_core_ack_cb, hg_core_handle,
        hg_core_handle->ack_buf, buf_size, hg_core_handle->ack_buf_plugin_data,
        hg_core_handle->na_addr, hg_core_handle->core_handle.info.context_id,
        hg_core_handle->tag, hg_core_handle->na_ack_op_id);
    /* Expected sends should always succeed after retry */
    HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "Could not post send for ack buffer (%s)",
        NA_Error_to_string(na_ret));

    return;

error:
    if (hg_core_handle->ack_buf != NULL) {
        NA_Msg_buf_free(hg_core_handle->na_class, hg_core_handle->ack_buf,
            hg_core_handle->ack_buf_plugin_data);
        hg_core_handle->ack_buf = NULL;
        hg_core_handle->ack_buf_plugin_data = NULL;
    }
    /* Mark handle as errored */
    if (ret != HG_CANCELED)
        hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);
    hg_atomic_cas32(
        &hg_core_handle->ret_status, (int32_t) HG_SUCCESS, (int32_t) ret);

    /* Complete and add to completion queue */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_ack_cb(const struct na_cb_info *callback_info)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) callback_info->arg;

    if (callback_info->ret == NA_SUCCESS) {
        /* Nothing */
    } else if (callback_info->ret == NA_CANCELED) {
        HG_CHECK_SUBSYS_WARNING(rpc,
            hg_atomic_get32(&hg_core_handle->status) & HG_CORE_OP_COMPLETED,
            "Operation was completed");
        HG_LOG_SUBSYS_DEBUG(
            rpc, "NA_CANCELED event on handle %p", (void *) hg_core_handle);

        hg_atomic_cas32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS,
            (int32_t) HG_CANCELED);
    } else {
        /* Mark handle as errored */
        hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_ERRORED);

        /* Keep first non-success ret status */
        hg_atomic_cas32(&hg_core_handle->ret_status, (int32_t) HG_SUCCESS,
            (int32_t) callback_info->ret);
        HG_LOG_SUBSYS_ERROR(rpc, "NA callback returned error (%s)",
            NA_Error_to_string(callback_info->ret));
    }

    /* Complete operation */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_process(struct hg_core_private_handle *hg_core_handle)
{
    struct hg_core_rpc_info *hg_core_rpc_info;
    int32_t HG_DEBUG_LOG_USED ref_count;
    hg_return_t ret;

    /* Already cached for self RPCs */
    if (hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_SELF_FORWARD)
        hg_core_rpc_info = hg_core_handle->core_handle.rpc_info;
    else {
        /* Retrieve exe function from function map */
        hg_core_rpc_info =
            hg_core_map_lookup(&HG_CORE_HANDLE_CLASS(hg_core_handle)->rpc_map,
                &hg_core_handle->core_handle.info.id);
        if (hg_core_rpc_info == NULL) {
            HG_LOG_SUBSYS_WARNING(rpc,
                "Could not find RPC ID (%" PRIu64 ") in RPC map",
                hg_core_handle->core_handle.info.id);
            HG_GOTO_DONE(error, ret, HG_NOENTRY);
        }
        // HG_CHECK_SUBSYS_ERROR(rpc, hg_core_rpc_info == NULL, error, ret,
        // HG_NOENTRY,
        //     "Could not find RPC ID (%" PRIu64 ") in RPC map",
        //     hg_core_handle->core_handle.info.id);

        /* Cache RPC info */
        hg_core_handle->core_handle.rpc_info = hg_core_rpc_info;
    }
    HG_CHECK_SUBSYS_ERROR(rpc, hg_core_rpc_info->rpc_cb == NULL, error, ret,
        HG_INVALID_ARG, "No RPC callback registered");

    /* Increment ref count here so that a call to HG_Destroy in user's RPC
     * callback does not free the handle but only schedules its completion
     */
    ref_count = hg_atomic_incr32(&hg_core_handle->ref_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count incr to %" PRId32,
        (void *) hg_core_handle, ref_count);

    /* Execute RPC callback */
    ret = hg_core_rpc_info->rpc_cb((hg_core_handle_t) hg_core_handle);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Error while executing RPC callback");

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_complete_op(struct hg_core_private_handle *hg_core_handle)
{
    int32_t op_completed_count =
                hg_atomic_incr32(&hg_core_handle->op_completed_count),
            op_expected_count =
                hg_atomic_get32(&hg_core_handle->op_expected_count);

    HG_LOG_SUBSYS_DEBUG(rpc_ref,
        "Completed %" PRId32 "/%" PRId32 " NA operations for handle (%p)",
        op_completed_count, op_expected_count, (void *) hg_core_handle);

    /* Add handle to completion queue when expected operations have
     * completed */
    if (op_completed_count == op_expected_count)
        hg_core_complete(hg_core_handle,
            (hg_return_t) hg_atomic_get32(&hg_core_handle->ret_status));
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_complete(struct hg_core_private_handle *hg_core_handle, hg_return_t ret)
{
    /* Mark op id as completed, also mark the operation as queued to track
     * when it will be released from the completion queue. */
    hg_atomic_or32(
        &hg_core_handle->status, HG_CORE_OP_COMPLETED | HG_CORE_OP_QUEUED);

    /* Forward status to callback */
    hg_core_handle->ret = ret;

    hg_core_handle->hg_completion_entry.op_type = HG_RPC;
    hg_core_handle->hg_completion_entry.op_id.hg_core_handle =
        (hg_core_handle_t) hg_core_handle;

    hg_core_completion_add(hg_core_handle->core_handle.info.context,
        &hg_core_handle->hg_completion_entry,
        hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_SELF_FORWARD);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
hg_core_loopback_event_set(struct hg_core_private_context *context)
{
    hg_return_t ret;
    int rc;

    hg_atomic_incr32(&context->loopback_notify.nevents);

    rc = hg_event_set(context->loopback_notify.event);
    HG_CHECK_SUBSYS_ERROR(poll, rc != HG_UTIL_SUCCESS, error, ret,
        HG_PROTOCOL_ERROR, "Could not signal completion queue");

    return HG_SUCCESS;

error:
    hg_atomic_decr32(&context->loopback_notify.nevents);

    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE hg_return_t
hg_core_loopback_event_get(
    struct hg_core_private_context *context, bool *notified_p)
{
    hg_return_t ret;
    bool signaled;
    int rc;

    rc = hg_event_get(context->loopback_notify.event, &signaled);
    HG_CHECK_SUBSYS_ERROR(poll, rc != HG_UTIL_SUCCESS, error, ret,
        HG_PROTOCOL_ERROR, "Could not get loopback event notification");
    if (signaled)
        hg_atomic_decr32(&context->loopback_notify.nevents);

    if (notified_p != NULL)
        *notified_p = signaled;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
hg_core_completion_add(struct hg_core_context *core_context,
    struct hg_completion_entry *hg_completion_entry, bool loopback_notify)
{
    struct hg_core_private_context *context =
        (struct hg_core_private_context *) core_context;
    struct hg_core_completion_queue *backfill_queue = &context->backfill_queue;
    int rc;

#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    /* Increment counter */
    if (hg_completion_entry->op_type == HG_BULK)
        hg_atomic_incr64(HG_CORE_CONTEXT_CLASS(context)->counters.bulk_count);
#endif

    rc = hg_atomic_queue_push(context->completion_queue, hg_completion_entry);
    if (rc != HG_UTIL_SUCCESS) {
        HG_LOG_SUBSYS_WARNING(perf, "Atomic completion queue is full, pushing "
                                    "completion data to backfill queue");

        /* Queue is full */
        hg_thread_mutex_lock(&backfill_queue->mutex);
        STAILQ_INSERT_TAIL(&backfill_queue->queue, hg_completion_entry, entry);
        hg_atomic_incr32(&backfill_queue->count);
        hg_thread_mutex_unlock(&backfill_queue->mutex);
    }

    /* Callback is pushed to the completion queue when something completes
     * so wake up anyone waiting in trigger */
    hg_thread_mutex_lock(&backfill_queue->mutex);
    hg_thread_cond_signal(&backfill_queue->cond);
    hg_thread_mutex_unlock(&backfill_queue->mutex);

    /* Do not bother notifying if it's not needed as any event call will
     * increase latency */
    if (loopback_notify && (context->loopback_notify.event > 0) &&
        hg_atomic_get32(&context->loopback_notify.must_notify))
        (void) hg_core_loopback_event_set(context);
}

/*---------------------------------------------------------------------------*/
static struct hg_completion_entry *
hg_core_completion_get(struct hg_core_private_context *context)
{
    struct hg_completion_entry *hg_completion_entry = NULL;

    hg_completion_entry = hg_atomic_queue_pop_mc(context->completion_queue);
    if (hg_completion_entry == NULL) { /* Check backfill queue */
        struct hg_core_completion_queue *backfill_queue =
            &context->backfill_queue;

        if (hg_atomic_get32(&backfill_queue->count) > 0) {
            hg_thread_mutex_lock(&backfill_queue->mutex);
            if (hg_atomic_get32(&backfill_queue->count) > 0) {
                hg_completion_entry = STAILQ_FIRST(&backfill_queue->queue);
                STAILQ_REMOVE_HEAD(&backfill_queue->queue, entry);
                hg_atomic_decr32(&backfill_queue->count);
            }
            hg_thread_mutex_unlock(&backfill_queue->mutex);
        }
    }

    return hg_completion_entry;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_completion_wait(
    struct hg_core_private_context *context, unsigned int timeout_ms)
{
    struct hg_core_completion_queue *backfill_queue = &context->backfill_queue;
    hg_return_t ret = HG_SUCCESS;

    hg_thread_mutex_lock(&backfill_queue->mutex);
    if ((hg_core_completion_count(context) == 0) &&
        (hg_thread_cond_timedwait(&backfill_queue->cond, &backfill_queue->mutex,
             timeout_ms) != HG_UTIL_SUCCESS))
        ret = HG_TIMEOUT;
    hg_thread_mutex_unlock(&backfill_queue->mutex);

    return ret;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE unsigned int
hg_core_completion_count(const struct hg_core_private_context *context)
{
    return hg_atomic_queue_count(context->completion_queue) +
           (unsigned int) hg_atomic_get32(&context->backfill_queue.count);
}

/*---------------------------------------------------------------------------*/
static void
hg_core_completion_trigger(struct hg_completion_entry *hg_completion_entry)
{
    /* Trigger entry */
    switch (hg_completion_entry->op_type) {
        case HG_ADDR:
            hg_core_trigger_lookup_entry(
                hg_completion_entry->op_id.hg_core_op_id);
            break;
        case HG_RPC:
            hg_core_trigger_entry(
                (struct hg_core_private_handle *)
                    hg_completion_entry->op_id.hg_core_handle);
            break;
        case HG_BULK:
            hg_bulk_trigger_entry(hg_completion_entry->op_id.hg_bulk_op_id);
            break;
        default:
            HG_LOG_SUBSYS_ERROR(poll, "Invalid type of completion entry (%d)",
                (int) hg_completion_entry->op_type);
            break;
    }
}

/*---------------------------------------------------------------------------*/
static HG_INLINE bool
hg_core_event_ready_loopback(struct hg_core_private_context *context)
{
    if (context->loopback_notify.event > 0) {
        /* We will need to notify the event if we're waiting */
        hg_atomic_cas32(&context->loopback_notify.must_notify, 0, 1);
        if (hg_core_completion_count(context) > 0) {
            hg_atomic_cas32(&context->loopback_notify.must_notify, 1, 0);
            return true;
        }
    }

    return false;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_progress_wait(
    struct hg_core_private_context *context, unsigned int timeout_ms)
{
    hg_time_t deadline, now = hg_time_from_ms(0);
    hg_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    do {
        bool safe_wait = false, progressed = false;
        unsigned int poll_timeout = 0;

        /* Bypass notifications if timeout_ms is 0 to prevent system calls */
        if (timeout_ms == 0) {
            ; // nothing to do
        } else if (context->poll_set) {
            if (!HG_Core_event_ready(&context->core_context)) {
                safe_wait = true;
                poll_timeout = hg_time_to_ms(hg_time_subtract(deadline, now));
            }
        } else if (!HG_CORE_CONTEXT_CLASS(context)->init_info.loopback &&
                   !HG_Core_event_ready(&context->core_context)) {
            /* This is the case for NA plugins that don't expose a fd */
            poll_timeout = hg_time_to_ms(hg_time_subtract(deadline, now));
        }

        /* Only enter blocking wait if it is safe to */
        if (safe_wait) {
            ret = hg_core_poll_wait(context, poll_timeout, &progressed);
            HG_CHECK_SUBSYS_HG_ERROR(poll, error, ret,
                "Could not make blocking progress on context");
        } else {
            ret = hg_core_progress_legacy(context, poll_timeout, &progressed);
            HG_CHECK_SUBSYS_HG_ERROR(poll, error, ret,
                "Could not make non-blocking progress on context");
        }

        /* We progressed or we have something to trigger */
        if (progressed || (hg_core_completion_count(context) > 0))
            return HG_SUCCESS;

        if (timeout_ms != 0)
            hg_time_get_current_ms(&now);
    } while (hg_time_less(now, deadline));

    return HG_TIMEOUT;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_poll_wait(struct hg_core_private_context *context,
    unsigned int timeout_ms, bool *progressed_p)
{
    struct hg_poll_event poll_events[HG_CORE_MAX_EVENTS]; /* Poll events */
    unsigned int i, nevents;
    hg_return_t ret;
    bool progressed = false;
    int rc;

    rc = hg_poll_wait(context->poll_set, timeout_ms, HG_CORE_MAX_EVENTS,
        poll_events, &nevents);

    /* There is no need to notify while we're in progress */
    hg_atomic_cas32(&context->loopback_notify.must_notify, 1, 0);

    HG_CHECK_SUBSYS_ERROR(poll, rc != HG_UTIL_SUCCESS, error, ret,
        HG_PROTOCOL_ERROR, "hg_poll_wait() failed");

    if (nevents == 1 && (poll_events[0].events & HG_POLLINTR)) {
        HG_LOG_SUBSYS_DEBUG(poll_loop, "Interrupted");
        *progressed_p = false;
        return HG_SUCCESS;
    }

    /* Process events */
    for (i = 0; i < nevents; i++) {
        bool progressed_event = false;
        unsigned int count = 0;

        switch (poll_events[i].data.u32) {
            case HG_CORE_POLL_LOOPBACK:
                HG_LOG_SUBSYS_DEBUG(poll_loop, "HG_CORE_POLL_LOOPBACK event");
                ret = hg_core_loopback_event_get(context, &progressed_event);
                HG_CHECK_SUBSYS_HG_ERROR(
                    poll, error, ret, "hg_core_loopback_event_get() failed");
                break;
#ifdef NA_HAS_SM
            case HG_CORE_POLL_SM:
                HG_LOG_SUBSYS_DEBUG(poll_loop, "HG_CORE_POLL_SM event");

                ret = hg_core_progress_na(
                    HG_CORE_CONTEXT_CLASS(context)->core_class.na_sm_class,
                    context->core_context.na_sm_context, &count);
                HG_CHECK_SUBSYS_HG_ERROR(
                    poll, error, ret, "hg_core_progress_na() failed");
                break;
#endif
            case HG_CORE_POLL_NA:
                HG_LOG_SUBSYS_DEBUG(poll_loop, "HG_CORE_POLL_NA event");

                ret = hg_core_progress_na(
                    HG_CORE_CONTEXT_CLASS(context)->core_class.na_class,
                    context->core_context.na_context, &count);
                HG_CHECK_SUBSYS_HG_ERROR(
                    poll, error, ret, "hg_core_progress_na() failed");
                break;
            default:
                HG_GOTO_SUBSYS_ERROR(poll, error, ret, HG_INVALID_ARG,
                    "Invalid type of poll event (%d)",
                    (int) poll_events[i].data.u32);
        }
        progressed |= progressed_event;
    }

    *progressed_p = progressed;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_progress(struct hg_core_private_context *context, unsigned int *count_p)
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_CONTEXT_CLASS(context);
    hg_return_t ret;

    /* Read loopback events if any */
    if (context->loopback_notify.event > 0) {
        /* There is no need to notify while we're in progress */
        hg_atomic_cas32(&context->loopback_notify.must_notify, 1, 0);

        if (hg_atomic_get32(&context->loopback_notify.nevents) > 0) {
            ret = hg_core_loopback_event_get(context, NULL);
            HG_CHECK_SUBSYS_HG_ERROR(
                poll, error, ret, "hg_core_loopback_event_get() failed");
        }
    }

#ifdef NA_HAS_SM
    /* Read NA SM events */
    if (context->core_context.na_sm_context) {
        ret = hg_core_progress_na(hg_core_class->core_class.na_sm_class,
            context->core_context.na_sm_context, NULL);
        HG_CHECK_SUBSYS_HG_ERROR(
            poll, error, ret, "hg_core_progress_na() failed");
    }
#endif

    /* Read NA events */
    ret = hg_core_progress_na(hg_core_class->core_class.na_class,
        context->core_context.na_context, NULL);
    HG_CHECK_SUBSYS_HG_ERROR(poll, error, ret, "hg_core_progress_na() failed");

    if (count_p != NULL)
        *count_p = hg_core_completion_count(context);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_progress_legacy(struct hg_core_private_context *context,
    unsigned int timeout_ms, bool *progressed_p)
{
    struct hg_core_private_class *hg_core_class =
        HG_CORE_CONTEXT_CLASS(context);
    bool progressed = false;
    unsigned int count = 0;
    unsigned int timeout;
    hg_return_t ret;

#ifdef NA_HAS_SM
    /* Poll over SM first if set */
    if (context->core_context.na_sm_context) {
        ret = hg_core_progress_na(hg_core_class->core_class.na_sm_class,
            context->core_context.na_sm_context, &count);
        HG_CHECK_SUBSYS_HG_ERROR(
            poll, error, ret, "hg_core_progress_na() failed");

        progressed |= (count > 0);
        count = 0;
        timeout = 0;
    } else {
#endif
        timeout = timeout_ms;
#ifdef NA_HAS_SM
    }
#endif

    /* Poll over defaut NA */
    if (timeout == 0) {
        ret = hg_core_progress_na(hg_core_class->core_class.na_class,
            context->core_context.na_context, &count);
        HG_CHECK_SUBSYS_HG_ERROR(
            poll, error, ret, "hg_core_progress_na() failed");
        progressed |= (count > 0);
    } else {
        bool progressed_na = false;
        ret = hg_core_progress_wait_na(hg_core_class->core_class.na_class,
            context->core_context.na_context, timeout, &progressed_na);
        HG_CHECK_SUBSYS_HG_ERROR(
            poll, error, ret, "hg_core_progress_wait_na() failed");
        progressed |= progressed_na;
    }

    *progressed_p = progressed;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_progress_wait_na(na_class_t *na_class, na_context_t *na_context,
    unsigned int timeout_ms, bool *progressed_p)
{
    hg_time_t deadline, now = hg_time_from_ms(0);
    unsigned int completed_count = 0,
                 poll_count = NA_Context_get_completion_count(na_context);
    bool progressed = false;
    hg_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    for (;;) {
        unsigned int actual_count = 0;
        na_return_t na_ret;

        /* Trigger everything we can from NA, if something completed it will
         * be moved to the HG context completion queue */
        na_ret = NA_Trigger(na_context, poll_count, &actual_count);
        HG_CHECK_SUBSYS_ERROR(poll, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_Trigger() failed (%s)",
            NA_Error_to_string(na_ret));
        completed_count += actual_count;

        /* Progressed */
        if (completed_count > 0) {
            progressed = true;
            break;
        }

        /* Make sure that timeout of 0 enters progress */
        if (timeout_ms != 0 && !hg_time_less(now, deadline))
            break;

        /* Otherwise try to make progress on NA */
        na_ret = NA_Poll_wait(na_class, na_context,
            hg_time_to_ms(hg_time_subtract(deadline, now)), &poll_count);

        if (na_ret == NA_TIMEOUT)
            break;

        HG_CHECK_SUBSYS_ERROR(poll, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "NA_Progress() failed (%s)",
            NA_Error_to_string(na_ret));

        if (timeout_ms != 0)
            hg_time_get_current_ms(&now);
    }

    *progressed_p = progressed;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_progress_na(
    na_class_t *na_class, na_context_t *na_context, unsigned int *count_p)
{
    unsigned int poll_count = 0;
    na_return_t na_ret;
    hg_return_t ret;

    /* Poll from NA */
    na_ret = NA_Poll(na_class, na_context, &poll_count);
    HG_CHECK_SUBSYS_ERROR(poll, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "NA_Poll() failed (%s)",
        NA_Error_to_string(na_ret));

    /* Trigger any callback from NA, if something completed it will
     * be moved to the HG context completion queue */
    na_ret = NA_Trigger(na_context, poll_count, count_p);
    HG_CHECK_SUBSYS_ERROR(poll, na_ret != NA_SUCCESS, error, ret,
        (hg_return_t) na_ret, "NA_Trigger() failed (%s)",
        NA_Error_to_string(na_ret));

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_trigger_wait(struct hg_core_private_context *context,
    unsigned int timeout_ms, unsigned int max_count,
    unsigned int *actual_count_p)
{
    hg_time_t deadline, now = hg_time_from_ms(0);
    unsigned int count = 0;
    hg_return_t ret = HG_SUCCESS;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    while (count < max_count) {
        struct hg_completion_entry *hg_completion_entry = NULL;

        /* Grab entry from completion queue */
        hg_completion_entry = hg_core_completion_get(context);
        if (hg_completion_entry == NULL) {
            /* If something was already processed leave */
            if (count > 0)
                break;

            /* Timeout is 0 so leave */
            if (!hg_time_less(now, deadline)) {
                ret = HG_TIMEOUT;
                break;
            }

            /* Otherwise wait remaining ms */
            ret = hg_core_completion_wait(
                context, hg_time_to_ms(hg_time_subtract(deadline, now)));
            if (ret == HG_TIMEOUT) /* Timeout occurred so leave */
                break;

            if (timeout_ms != 0)
                hg_time_get_current_ms(&now);
            continue; /* Give another change to grab it */
        }

        /* Completion queue should not be empty now */
        HG_CHECK_SUBSYS_ERROR(poll, hg_completion_entry == NULL, done, ret,
            HG_FAULT, "NULL completion entry");

        /* Trigger entry */
        hg_core_completion_trigger(hg_completion_entry);

        count++;
    }

    if (actual_count_p)
        *actual_count_p = count;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_core_trigger(struct hg_core_private_context *context, unsigned int max_count,
    unsigned int *actual_count_p)
{
    unsigned int count = 0;

    while (count < max_count) {
        struct hg_completion_entry *hg_completion_entry;

        /* Grab entry from completion queue */
        hg_completion_entry = hg_core_completion_get(context);
        if (hg_completion_entry == NULL)
            break;

        /* Trigger entry */
        hg_core_completion_trigger(hg_completion_entry);

        count++;
    }

    if (actual_count_p)
        *actual_count_p = count;
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_trigger_lookup_entry(struct hg_core_op_id *hg_core_op_id)
{
    /* Execute callback */
    if (hg_core_op_id->callback) {
        struct hg_core_cb_info hg_core_cb_info = {.arg = hg_core_op_id->arg,
            .ret = HG_SUCCESS,
            .type = HG_CB_LOOKUP,
            .info.lookup.addr =
                (hg_core_addr_t) hg_core_op_id->info.lookup.hg_core_addr};

        hg_core_op_id->callback(&hg_core_cb_info);
    }

    /* NB. OK to free after callback execution, op ID is not re-used */
    free(hg_core_op_id);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_trigger_entry(struct hg_core_private_handle *hg_core_handle)
{
    hg_atomic_and32(&hg_core_handle->status, ~HG_CORE_OP_QUEUED);

    HG_LOG_SUBSYS_DEBUG(rpc, "Triggering callback type %s",
        hg_core_op_type_to_string(hg_core_handle->op_type));

    hg_core_handle->ops.trigger(hg_core_handle);

    /* Reuse handle if we were listening, otherwise destroy it */
    (void) hg_core_destroy(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_trigger_self(struct hg_core_private_handle *hg_core_handle)
{
    switch (hg_core_handle->op_type) {
        case HG_CORE_PROCESS:
            hg_core_trigger_process(hg_core_handle);
            break;
        case HG_CORE_FORWARD:
            hg_core_trigger_forward_cb(hg_core_handle);
            break;
        case HG_CORE_RESPOND:
            hg_core_trigger_self_respond_cb(hg_core_handle);
            break;
        default:
            HG_LOG_SUBSYS_ERROR(rpc, "Invalid core operation type");
            break;
    }
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_trigger_na(struct hg_core_private_handle *hg_core_handle)
{
    switch (hg_core_handle->op_type) {
        case HG_CORE_PROCESS:
            hg_core_trigger_process(hg_core_handle);
            break;
        case HG_CORE_FORWARD:
            hg_core_trigger_forward_cb(hg_core_handle);
            break;
        case HG_CORE_RESPOND:
            hg_core_trigger_respond_cb(hg_core_handle);
            break;
        default:
            HG_LOG_SUBSYS_ERROR(rpc, "Invalid core operation type");
            break;
    }
}

/*---------------------------------------------------------------------------*/
static void
hg_core_trigger_process(struct hg_core_private_handle *hg_core_handle)
{
    hg_return_t ret;
    int32_t flags;

    /* Silently exit if error occurred */
    if (hg_core_handle->ret != HG_SUCCESS)
        return;

    flags = hg_atomic_get32(&hg_core_handle->flags);
    /* Take another reference to make sure the handle only gets freed
     * after the response is sent (when forwarding to self, always take a
     * refcount to make sure the handle does not get re-used too early) */
    if (!(flags & HG_CORE_NO_RESPONSE) || (flags & HG_CORE_SELF_FORWARD)) {
        int32_t HG_DEBUG_LOG_USED ref_count =
            hg_atomic_incr32(&hg_core_handle->ref_count);
        HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count incr to %" PRId32,
            (void *) hg_core_handle, ref_count);
    }

    /* Run RPC callback */
    ret = hg_core_process(hg_core_handle);
    if (ret != HG_SUCCESS && !(flags & HG_CORE_NO_RESPONSE)) {
        hg_size_t header_size =
            hg_core_header_response_get_size() +
            hg_core_handle->core_handle.na_out_header_offset;

        /* Respond in case of error */
        (void) hg_core_respond(hg_core_handle, NULL, NULL, 0, header_size, ret);
    }
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_trigger_forward_cb(struct hg_core_private_handle *hg_core_handle)
{
    if (hg_core_handle->request_callback) {
        struct hg_core_cb_info hg_core_cb_info = {
            .arg = hg_core_handle->request_arg,
            .ret = hg_core_handle->ret,
            .type = HG_CB_FORWARD,
            .info.forward.handle = (hg_core_handle_t) hg_core_handle};

        (void) hg_core_handle->request_callback(&hg_core_cb_info);
    }
}

/*---------------------------------------------------------------------------*/
static HG_INLINE void
hg_core_trigger_respond_cb(struct hg_core_private_handle *hg_core_handle)
{
    if (hg_core_handle->response_callback) {
        struct hg_core_cb_info hg_core_cb_info = {
            .arg = hg_core_handle->response_arg,
            .ret = hg_core_handle->ret,
            .type = HG_CB_RESPOND,
            .info.respond.handle = (hg_core_handle_t) hg_core_handle};

        (void) hg_core_handle->response_callback(&hg_core_cb_info);
    }
}

/*---------------------------------------------------------------------------*/
static void
hg_core_trigger_self_respond_cb(struct hg_core_private_handle *hg_core_handle)
{
    int32_t HG_DEBUG_LOG_USED ref_count, HG_DEBUG_LOG_USED expected_count;
    hg_return_t ret;

    /* Increment number of expected completions */
    expected_count = hg_atomic_incr32(&hg_core_handle->op_expected_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) expected_count incr to %" PRId32,
        (void *) hg_core_handle, expected_count);

    /* Increment refcount and push handle back to completion queue */
    ref_count = hg_atomic_incr32(&hg_core_handle->ref_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count incr to %" PRId32,
        (void *) hg_core_handle, ref_count);

    /* First execute response callback */
    if (hg_core_handle->response_callback) {
        struct hg_core_cb_info hg_core_cb_info = {
            .arg = hg_core_handle->response_arg,
            .ret = HG_SUCCESS, /* Keep return as SUCCESS */
            .type = HG_CB_RESPOND,
            .info.respond.handle = (hg_core_handle_t) hg_core_handle};

        (void) hg_core_handle->response_callback(&hg_core_cb_info);
    }

    /* Assign forward callback back to handle */
    hg_core_handle->op_type = HG_CORE_FORWARD;

    /* Process output */
    ret = hg_core_process_output(hg_core_handle, hg_core_more_data_complete);
    if (ret != HG_SUCCESS) {
        HG_LOG_SUBSYS_ERROR(rpc, "Could not process output");
        hg_atomic_set32(&hg_core_handle->ret_status, (int32_t) ret);
    }

    /* Mark as completed */
    hg_core_complete_op(hg_core_handle);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_core_cancel(struct hg_core_private_handle *hg_core_handle)
{
    hg_return_t ret;
    int32_t status;

    HG_CHECK_SUBSYS_ERROR(rpc,
        hg_atomic_get32(&hg_core_handle->flags) & HG_CORE_SELF_FORWARD, error,
        ret, HG_OPNOTSUPPORTED, "Local cancellation is not supported");

    /* Exit if op has already completed */
    status = hg_atomic_get32(&hg_core_handle->status);
    if ((status & HG_CORE_OP_COMPLETED) || (status & HG_CORE_OP_ERRORED) ||
        (status & HG_CORE_OP_CANCELED))
        return HG_SUCCESS;

    /* Let only one thread call NA_Cancel() */
    if (hg_atomic_or32(&hg_core_handle->status, HG_CORE_OP_CANCELED) &
        HG_CORE_OP_CANCELED)
        return HG_SUCCESS;

    /* Cancel all NA operations issued */
    if (hg_core_handle->na_recv_op_id != NULL) {
        na_return_t na_ret = NA_Cancel(hg_core_handle->na_class,
            hg_core_handle->na_context, hg_core_handle->na_recv_op_id);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not cancel recv op id (%s)",
            NA_Error_to_string(na_ret));
    }

    if (hg_core_handle->na_send_op_id != NULL) {
        na_return_t na_ret = NA_Cancel(hg_core_handle->na_class,
            hg_core_handle->na_context, hg_core_handle->na_send_op_id);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not cancel send op id (%s)",
            NA_Error_to_string(na_ret));
    }

    if (hg_core_handle->na_ack_op_id != NULL) {
        na_return_t na_ret = NA_Cancel(hg_core_handle->na_class,
            hg_core_handle->na_context, hg_core_handle->na_ack_op_id);
        HG_CHECK_SUBSYS_ERROR(rpc, na_ret != NA_SUCCESS, error, ret,
            (hg_return_t) na_ret, "Could not cancel ack op id (%s)",
            NA_Error_to_string(na_ret));
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_get_na_protocol_info(
    const char *info_string, struct na_protocol_info **na_protocol_info_p)
{
    return (hg_return_t) NA_Get_protocol_info(info_string, na_protocol_info_p);
}

/*---------------------------------------------------------------------------*/
void
HG_Core_free_na_protocol_info(struct na_protocol_info *na_protocol_info)
{
    NA_Free_protocol_info(na_protocol_info);
}

/*---------------------------------------------------------------------------*/
hg_core_class_t *
HG_Core_init(const char *na_info_string, uint8_t na_listen)
{
    struct hg_core_private_class *hg_core_class;
    hg_return_t ret;

    HG_LOG_SUBSYS_DEBUG(
        cls, "Initializing with %s, listen=%d", na_info_string, na_listen);

    ret = hg_core_init(na_info_string, na_listen, 0, NULL, &hg_core_class);
    HG_CHECK_SUBSYS_HG_ERROR(cls, error, ret, "Cannot initialize core class");

    HG_LOG_SUBSYS_DEBUG(
        cls, "Initialized core class (%p)", (void *) hg_core_class);

    return (hg_core_class_t *) hg_core_class;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
hg_core_class_t *
HG_Core_init_opt(const char *na_info_string, uint8_t na_listen,
    const struct hg_init_info *hg_init_info)
{
    struct hg_core_private_class *hg_core_class;
    hg_return_t ret;

    HG_LOG_SUBSYS_DEBUG(
        cls, "Initializing with %s, listen=%d", na_info_string, na_listen);

    /* v2.2 is latest version for which init struct was not versioned */
    ret = hg_core_init(na_info_string, na_listen, HG_VERSION(2, 2),
        hg_init_info, &hg_core_class);
    HG_CHECK_SUBSYS_HG_ERROR(cls, error, ret, "Cannot initialize core class");

    HG_LOG_SUBSYS_DEBUG(
        cls, "Initialized core class (%p)", (void *) hg_core_class);

    return (hg_core_class_t *) hg_core_class;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
hg_core_class_t *
HG_Core_init_opt2(const char *na_info_string, uint8_t na_listen,
    unsigned int version, const struct hg_init_info *hg_init_info)
{
    struct hg_core_private_class *hg_core_class;
    hg_return_t ret;

    HG_LOG_SUBSYS_DEBUG(
        cls, "Initializing with %s, listen=%d", na_info_string, na_listen);

    ret = hg_core_init(
        na_info_string, na_listen, version, hg_init_info, &hg_core_class);
    HG_CHECK_SUBSYS_HG_ERROR(cls, error, ret, "Cannot initialize core class");

    HG_LOG_SUBSYS_DEBUG(
        cls, "Initialized core class (%p)", (void *) hg_core_class);

    return (hg_core_class_t *) hg_core_class;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_finalize(hg_core_class_t *hg_core_class)
{
    hg_return_t ret;

    HG_LOG_SUBSYS_DEBUG(
        cls, "Finalizing core class (%p)", (void *) hg_core_class);

    ret = hg_core_finalize((struct hg_core_private_class *) hg_core_class);
    HG_CHECK_SUBSYS_HG_ERROR(cls, error, ret,
        "Cannot finalize HG core class (%p)", (void *) hg_core_class);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
HG_Core_cleanup(void)
{
    NA_Cleanup();
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_set_more_data_callback(struct hg_core_class *hg_core_class,
    hg_return_t (*more_data_acquire_callback)(hg_core_handle_t, hg_op_t,
        void (*done_callback)(hg_core_handle_t, hg_return_t)),
    void (*more_data_release_callback)(hg_core_handle_t))
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");

    private_class->more_data_cb.acquire = more_data_acquire_callback;
    private_class->more_data_cb.release = more_data_release_callback;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_class_get_counters(const hg_core_class_t *hg_core_class,
    struct hg_diag_counters *diag_counters)
{
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    const struct hg_core_private_class *private_class =
        (const struct hg_core_private_class *) hg_core_class;
#endif
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");
    HG_CHECK_SUBSYS_ERROR(cls, diag_counters == NULL, error, ret,
        HG_INVALID_ARG, "NULL pointer to diag_counters");
#if defined(HG_HAS_DEBUG) && !defined(_WIN32)
    hg_core_class_get_counters(&private_class->counters, diag_counters);
#else
    HG_LOG_SUBSYS_ERROR(cls, "Counters not supported in current build, please "
                             "build with MERCURY_ENABLE_DEBUG");
    return HG_OPNOTSUPPORTED;
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_core_context_t *
HG_Core_context_create(hg_core_class_t *hg_core_class)
{
    struct hg_core_private_context *context;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR_NORET(
        ctx, hg_core_class == NULL, error, "NULL HG core class");

    HG_LOG_SUBSYS_DEBUG(ctx, "Creating new context with id=%u", 0);

    ret = hg_core_context_create(
        (struct hg_core_private_class *) hg_core_class, 0, &context);
    HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not create context");

    HG_LOG_SUBSYS_DEBUG(ctx, "Created new context (%p)", (void *) context);

    return (hg_core_context_t *) context;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
hg_core_context_t *
HG_Core_context_create_id(hg_core_class_t *hg_core_class, uint8_t id)
{
    struct hg_core_private_context *context;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR_NORET(
        ctx, hg_core_class == NULL, error, "NULL HG core class");

    HG_LOG_SUBSYS_DEBUG(ctx, "Creating new context with id=%u", id);

    ret = hg_core_context_create(
        (struct hg_core_private_class *) hg_core_class, id, &context);
    HG_CHECK_SUBSYS_HG_ERROR(
        ctx, error, ret, "Could not create context with id=%u", id);

    HG_LOG_SUBSYS_DEBUG(ctx, "Created new context (%p)", (void *) context);

    return (hg_core_context_t *) context;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_context_destroy(hg_core_context_t *context)
{
    hg_return_t ret;

    HG_LOG_SUBSYS_DEBUG(ctx, "Destroying context (%p)", (void *) context);

    ret = hg_core_context_destroy((struct hg_core_private_context *) context);
    HG_CHECK_SUBSYS_HG_ERROR(
        ctx, error, ret, "Could not destroy context (%p)", (void *) context);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
unsigned int
HG_Core_context_get_completion_count(const hg_core_context_t *context)
{
    HG_CHECK_SUBSYS_ERROR_NORET(
        ctx, context == NULL, error, "NULL HG core context");

    return hg_core_completion_count(
        (const struct hg_core_private_context *) context);

error:
    return 0;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_context_set_handle_create_callback(hg_core_context_t *context,
    hg_return_t (*callback)(hg_core_handle_t, void *), void *arg)
{
    struct hg_core_private_context *private_context =
        (struct hg_core_private_context *) context;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(ctx, context == NULL, error, ret, HG_INVALID_ARG,
        "NULL HG core context");

    private_context->handle_create_cb.callback = callback;
    private_context->handle_create_cb.arg = arg;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_context_post(hg_core_context_t *context)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(ctx, context == NULL, error, ret, HG_INVALID_ARG,
        "NULL HG core context");

    ret = hg_core_context_post((struct hg_core_private_context *) context);
    HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not post context");

    HG_LOG_SUBSYS_DEBUG(
        ctx, "Pre-posted handles on context (%p)", (void *) context);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_context_unpost(hg_core_context_t *context)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(ctx, context == NULL, error, ret, HG_INVALID_ARG,
        "NULL HG core context");

    ret = hg_core_context_unpost((struct hg_core_private_context *) context,
        HG_CORE_CLEANUP_TIMEOUT * 10);
    HG_CHECK_SUBSYS_HG_ERROR(ctx, error, ret, "Could not unpost context");

    HG_LOG_SUBSYS_DEBUG(
        ctx, "Unposted handles on context (%p)", (void *) context);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_register(
    hg_core_class_t *hg_core_class, hg_id_t id, hg_core_rpc_cb_t rpc_cb)
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    struct hg_core_rpc_info *hg_core_rpc_info;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");

    /* Check if registered and set RPC CB */
    hg_core_rpc_info = hg_core_map_lookup(&private_class->rpc_map, &id);
    if (hg_core_rpc_info == NULL) {
        HG_LOG_SUBSYS_DEBUG(cls, "Inserting new RPC ID (%" PRIu64 ")", id);

        ret =
            hg_core_map_insert(&private_class->rpc_map, &id, &hg_core_rpc_info);
        HG_CHECK_SUBSYS_HG_ERROR(
            cls, error, ret, "Could not insert new RPC ID (%" PRIu64 ")", id);
    } else
        HG_LOG_SUBSYS_WARNING(cls,
            "Overwriting RPC callback for a previously registered RPC ID "
            "(%" PRIu64 ")",
            id);

    hg_core_rpc_info->rpc_cb = rpc_cb;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_deregister(hg_core_class_t *hg_core_class, hg_id_t id)
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");

    ret = hg_core_map_remove(&private_class->rpc_map, &id);
    HG_CHECK_SUBSYS_HG_ERROR(cls, error, ret,
        "Could not deregister RPC ID (%" PRIu64 ") from function map", id);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_registered(hg_core_class_t *hg_core_class, hg_id_t id, uint8_t *flag_p)
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");
    HG_CHECK_SUBSYS_ERROR(
        cls, flag_p == NULL, error, ret, HG_INVALID_ARG, "NULL flag pointer");

    *flag_p =
        (uint8_t) (hg_core_map_lookup(&private_class->rpc_map, &id) != NULL);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_register_data(hg_core_class_t *hg_core_class, hg_id_t id, void *data,
    void (*free_callback)(void *))
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    struct hg_core_rpc_info *hg_core_rpc_info = NULL;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");

    hg_core_rpc_info = hg_core_map_lookup(&private_class->rpc_map, &id);
    HG_CHECK_SUBSYS_ERROR(cls, hg_core_rpc_info == NULL, error, ret, HG_NOENTRY,
        "Could not find RPC ID (%" PRIu64 ") in RPC map", id);

    /* We assume that only one thread will edit data for a given RPC ID */
    HG_CHECK_SUBSYS_WARNING(cls, hg_core_rpc_info->data != NULL,
        "Overwriting data previously registered for RPC ID (%" PRIu64 ")", id);
    hg_core_rpc_info->data = data;
    hg_core_rpc_info->free_callback = free_callback;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void *
HG_Core_registered_data(hg_core_class_t *hg_core_class, hg_id_t id)
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    struct hg_core_rpc_info *hg_core_rpc_info = NULL;

    HG_CHECK_SUBSYS_ERROR_NORET(
        cls, hg_core_class == NULL, error, "NULL HG core class");

    hg_core_rpc_info = hg_core_map_lookup(&private_class->rpc_map, &id);
    HG_CHECK_SUBSYS_ERROR_NORET(cls, hg_core_rpc_info == NULL, error,
        "Could not find RPC ID (%" PRIu64 ") in RPC map", id);

    return hg_core_rpc_info->data;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_registered_disable_response(
    hg_core_class_t *hg_core_class, hg_id_t id, uint8_t disable)
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    struct hg_core_rpc_info *hg_core_rpc_info = NULL;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");

    hg_core_rpc_info = hg_core_map_lookup(&private_class->rpc_map, &id);
    HG_CHECK_SUBSYS_ERROR(cls, hg_core_rpc_info == NULL, error, ret, HG_NOENTRY,
        "Could not find RPC ID (%" PRIu64 ") in RPC map", id);

    hg_core_rpc_info->no_response = disable;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_registered_disabled_response(
    hg_core_class_t *hg_core_class, hg_id_t id, uint8_t *disabled_p)
{
    struct hg_core_private_class *private_class =
        (struct hg_core_private_class *) hg_core_class;
    struct hg_core_rpc_info *hg_core_rpc_info = NULL;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(cls, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");
    HG_CHECK_SUBSYS_ERROR(cls, disabled_p == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to disabled flag");

    hg_core_rpc_info = hg_core_map_lookup(&private_class->rpc_map, &id);
    HG_CHECK_SUBSYS_ERROR(cls, hg_core_rpc_info == NULL, error, ret, HG_NOENTRY,
        "Could not find RPC ID (%" PRIu64 ") in RPC map", id);

    *disabled_p = hg_core_rpc_info->no_response;

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_lookup1(hg_core_context_t *context, hg_core_cb_t callback,
    void *arg, const char *name, hg_core_op_id_t *op_id)
{
    struct hg_core_op_id *hg_core_op_id = NULL;
    struct hg_completion_entry *hg_completion_entry = NULL;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, context == NULL, error, ret, HG_INVALID_ARG,
        "NULL HG core context");
    HG_CHECK_SUBSYS_ERROR(
        addr, callback == NULL, error, ret, HG_INVALID_ARG, "NULL callback");
    HG_CHECK_SUBSYS_ERROR(
        addr, name == NULL, error, ret, HG_INVALID_ARG, "NULL lookup name");
    (void) op_id;

    HG_LOG_SUBSYS_DEBUG(addr, "Looking up \"%s\"", name);

    /* Allocate op_id */
    hg_core_op_id = (struct hg_core_op_id *) calloc(1, sizeof(*hg_core_op_id));
    HG_CHECK_SUBSYS_ERROR(addr, hg_core_op_id == NULL, error, ret, HG_NOMEM,
        "Could not allocate HG operation ID");
    hg_core_op_id->context = (struct hg_core_private_context *) context;
    hg_core_op_id->type = HG_CB_LOOKUP;
    hg_core_op_id->callback = callback;
    hg_core_op_id->arg = arg;
    hg_core_op_id->info.lookup.hg_core_addr = NULL;

    ret = hg_core_addr_lookup(
        (struct hg_core_private_class *) context->core_class, name,
        &hg_core_op_id->info.lookup.hg_core_addr);
    HG_CHECK_SUBSYS_HG_ERROR(
        addr, error, ret, "Could not lookup address for %s", name);

    HG_LOG_SUBSYS_DEBUG(addr, "Created new address (%p)",
        (void *) hg_core_op_id->info.lookup.hg_core_addr);

    /* Add callback to completion queue */
    hg_completion_entry = &hg_core_op_id->hg_completion_entry;
    hg_completion_entry->op_type = HG_ADDR;
    hg_completion_entry->op_id.hg_core_op_id = hg_core_op_id;

    hg_core_completion_add(context, hg_completion_entry, true);

    return HG_SUCCESS;

error:
    if (hg_core_op_id) {
        hg_core_addr_free(hg_core_op_id->info.lookup.hg_core_addr);
        free(hg_core_op_id);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_lookup2(
    hg_core_class_t *hg_core_class, const char *name, hg_core_addr_t *addr_p)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");
    HG_CHECK_SUBSYS_ERROR(
        addr, name == NULL, error, ret, HG_INVALID_ARG, "NULL lookup name");
    HG_CHECK_SUBSYS_ERROR(addr, addr_p == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to address");

    HG_LOG_SUBSYS_DEBUG(addr, "Looking up \"%s\"", name);

    ret = hg_core_addr_lookup((struct hg_core_private_class *) hg_core_class,
        name, (struct hg_core_private_addr **) addr_p);
    HG_CHECK_SUBSYS_HG_ERROR(
        addr, error, ret, "Could not lookup address for %s", name);

    HG_LOG_SUBSYS_DEBUG(addr, "Created new address (%p)", (void *) *addr_p);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_free(hg_core_addr_t addr)
{
    HG_LOG_SUBSYS_DEBUG(addr, "Freeing address (%p)", (void *) addr);

    hg_core_addr_free((struct hg_core_private_addr *) addr);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_set_remove(hg_core_addr_t addr)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, addr == HG_CORE_ADDR_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core address");

    ret = hg_core_addr_set_remove((struct hg_core_private_addr *) addr);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret,
        "Could not set address to be removed (%p)", (void *) addr);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_self(hg_core_class_t *hg_core_class, hg_core_addr_t *addr_p)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");
    HG_CHECK_SUBSYS_ERROR(addr, addr_p == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to core address");

    ret = hg_core_addr_self((struct hg_core_private_class *) hg_core_class,
        (struct hg_core_private_addr **) addr_p);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret, "Could not get self address");

    HG_LOG_SUBSYS_DEBUG(
        addr, "Created new self address (%p)", (void *) *addr_p);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_dup(hg_core_addr_t addr, hg_core_addr_t *new_addr_p)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, addr == HG_CORE_ADDR_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core address");
    HG_CHECK_SUBSYS_ERROR(addr, new_addr_p == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to dup addr");

    ret = hg_core_addr_dup((struct hg_core_private_addr *) addr,
        (struct hg_core_private_addr **) new_addr_p);
    HG_CHECK_SUBSYS_HG_ERROR(
        addr, error, ret, "Could not duplicate address (%p)", (void *) addr);

    HG_LOG_SUBSYS_DEBUG(addr, "Duped address (%p) to address (%p)",
        (void *) addr, (void *) *new_addr_p);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
uint8_t
HG_Core_addr_cmp(hg_core_addr_t addr1, hg_core_addr_t addr2)
{
    if (addr1 == HG_CORE_ADDR_NULL && addr2 == HG_CORE_ADDR_NULL)
        return HG_TRUE;

    if (addr1 == HG_CORE_ADDR_NULL || addr2 == HG_CORE_ADDR_NULL)
        return HG_FALSE;

    return hg_core_addr_cmp((struct hg_core_private_addr *) addr1,
        (struct hg_core_private_addr *) addr2);
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_to_string(char *buf, hg_size_t *buf_size, hg_core_addr_t addr)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, buf_size == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to buffer size");
    HG_CHECK_SUBSYS_ERROR(addr, addr == HG_CORE_ADDR_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core address");

    ret = hg_core_addr_to_string(
        buf, buf_size, (struct hg_core_private_addr *) addr);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret,
        "Could not convert address (%p) to string", (void *) addr);

    if (buf) {
        HG_LOG_SUBSYS_DEBUG(addr, "Generated string \"%s\" from address (%p)",
            buf, (void *) addr);
    }

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_size_t
HG_Core_addr_get_serialize_size(hg_core_addr_t addr, unsigned long flags)
{
    hg_size_t ret;

    HG_CHECK_SUBSYS_ERROR_NORET(
        addr, addr == HG_CORE_ADDR_NULL, error, "NULL HG core address");

    ret = hg_core_addr_get_serialize_size(
        (struct hg_core_private_addr *) addr, flags & 0xff);

    HG_LOG_SUBSYS_DEBUG(addr,
        "Serialize size is %" PRIu64 " bytes for address (%p)", ret,
        (void *) addr);

    return ret;

error:
    return 0;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_serialize(
    void *buf, hg_size_t buf_size, unsigned long flags, hg_core_addr_t addr)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, buf == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to buffer");
    HG_CHECK_SUBSYS_ERROR(
        addr, buf_size == 0, error, ret, HG_INVALID_ARG, "NULL buffer size");
    HG_CHECK_SUBSYS_ERROR(addr, addr == HG_CORE_ADDR_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core address");

    HG_LOG_SUBSYS_DEBUG(addr, "Serializing address (%p)", (void *) addr);

    ret = hg_core_addr_serialize(
        buf, buf_size, flags & 0xff, (struct hg_core_private_addr *) addr);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret, "Could not serialize address");

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_addr_deserialize(hg_core_class_t *hg_core_class, hg_core_addr_t *addr_p,
    const void *buf, hg_size_t buf_size)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(addr, hg_core_class == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core class");
    HG_CHECK_SUBSYS_ERROR(addr, addr_p == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to HG core address");
    HG_CHECK_SUBSYS_ERROR(addr, buf == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to buffer");
    HG_CHECK_SUBSYS_ERROR(
        addr, buf_size == 0, error, ret, HG_INVALID_ARG, "NULL buffer size");

    ret =
        hg_core_addr_deserialize((struct hg_core_private_class *) hg_core_class,
            (struct hg_core_private_addr **) addr_p, buf, buf_size);
    HG_CHECK_SUBSYS_HG_ERROR(addr, error, ret,
        "Could not deserialize address from (%p, %zu)", buf, (size_t) buf_size);

    HG_LOG_SUBSYS_DEBUG(
        addr, "Deserialized into new address (%p)", (void *) *addr_p);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_create(hg_core_context_t *context, hg_core_addr_t addr, hg_id_t id,
    hg_core_handle_t *handle_p)
{
    struct hg_core_private_handle *hg_core_handle = NULL;
    na_class_t *na_class = NULL;
    na_context_t *na_context = NULL;
    na_addr_t *na_addr = NULL;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc, context == NULL, error, ret, HG_INVALID_ARG,
        "NULL HG core context");
    HG_CHECK_SUBSYS_ERROR(rpc, handle_p == NULL, error, ret, HG_INVALID_ARG,
        "NULL pointer to HG core handle");

    HG_LOG_SUBSYS_DEBUG(rpc_ref,
        "Creating new handle with ID=%" PRIu64 ", address=%p", id,
        (void *) addr);

    /* Determine which NA class/context to use */
    ret = hg_core_resolve_na((struct hg_core_private_context *) context,
        (struct hg_core_private_addr *) addr, &na_class, &na_context, &na_addr);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not resolve NA components");

    /* Create new handle */
    ret = hg_core_create((struct hg_core_private_context *) context, na_class,
        na_context, HG_CORE_HANDLE_USER, &hg_core_handle);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not create HG core handle");

    /* Set addr / RPC ID */
    ret = hg_core_set_rpc(
        hg_core_handle, (struct hg_core_private_addr *) addr, na_addr, id);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret,
        "Could not set new RPC info to handle %p", (void *) hg_core_handle);

    HG_LOG_SUBSYS_DEBUG(
        rpc_ref, "Created new handle (%p)", (void *) hg_core_handle);

    *handle_p = (hg_core_handle_t) hg_core_handle;

    return HG_SUCCESS;

error:
    (void) hg_core_destroy(hg_core_handle);

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_destroy(hg_core_handle_t handle)
{
    hg_return_t ret;

    if (handle == HG_CORE_HANDLE_NULL)
        return HG_SUCCESS;

    HG_LOG_SUBSYS_DEBUG(rpc, "Destroying handle (%p)", (void *) handle);

    ret = hg_core_destroy((struct hg_core_private_handle *) handle);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not destroy handle (%p)", (void *) handle);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_reset(hg_core_handle_t handle, hg_core_addr_t addr, hg_id_t id)
{
    struct hg_core_private_handle *hg_core_handle =
        (struct hg_core_private_handle *) handle;
    na_class_t *na_class = NULL;
    na_context_t *na_context = NULL;
    na_addr_t *na_addr = NULL;
    hg_return_t ret;
    int32_t status;

    HG_CHECK_SUBSYS_ERROR(rpc, hg_core_handle == NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core handle");

    /* Not safe to reset unless in completed state */
    status = hg_atomic_get32(&hg_core_handle->status);
    HG_CHECK_SUBSYS_ERROR(rpc,
        !(status & HG_CORE_OP_COMPLETED) || (status & HG_CORE_OP_QUEUED), error,
        ret, HG_BUSY, "Cannot reset HG core handle, still in use (%p)",
        (void *) handle);

    HG_LOG_SUBSYS_DEBUG(rpc,
        "Resetting handle (%p) with ID=%" PRIu64 ", address (%p)",
        (void *) handle, id, (void *) addr);

    /* Determine which NA class/context to use */
    ret = hg_core_resolve_na(HG_CORE_HANDLE_CONTEXT(hg_core_handle),
        (struct hg_core_private_addr *) addr, &na_class, &na_context, &na_addr);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not resolve NA components");

    /* In that case, we must free and re-allocate NA resources */
    if (na_class != hg_core_handle->na_class) {
        HG_LOG_SUBSYS_WARNING(perf,
            "Releasing NA resource for this handle (%p)", (void *) handle);

        hg_core_free_na(hg_core_handle);

        ret = hg_core_alloc_na(hg_core_handle, na_class, na_context, 0);
        HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret,
            "Could not re-allocate NA resources for this handle (%p)",
            (void *) handle);
    }

    /* Reset handle */
    hg_core_reset(hg_core_handle);

    /* Set addr / RPC ID */
    ret = hg_core_set_rpc(
        hg_core_handle, (struct hg_core_private_addr *) addr, na_addr, id);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret,
        "Could not set new RPC info to handle %p", (void *) hg_core_handle);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_ref_incr(hg_core_handle_t handle)
{
    int32_t HG_DEBUG_LOG_USED ref_count;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc, handle == HG_CORE_HANDLE_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core handle");

    ref_count = hg_atomic_incr32(
        &((struct hg_core_private_handle *) handle)->ref_count);
    HG_LOG_SUBSYS_DEBUG(rpc_ref, "Handle (%p) ref_count incr to %" PRId32,
        (void *) handle, ref_count);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
int32_t
HG_Core_ref_get(hg_core_handle_t handle)
{
    HG_CHECK_SUBSYS_ERROR_NORET(
        rpc, handle == HG_CORE_HANDLE_NULL, error, "NULL HG core handle");

    return (int32_t) hg_atomic_get32(
        &((struct hg_core_private_handle *) handle)->ref_count);

error:
    return -1;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_release_input(hg_core_handle_t handle)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc, handle == HG_CORE_HANDLE_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core handle");

    HG_LOG_SUBSYS_DEBUG(rpc, "Releasing input on handle (%p)", (void *) handle);

    ret = hg_core_release_input((struct hg_core_private_handle *) handle);
    HG_CHECK_SUBSYS_HG_ERROR(rpc, error, ret,
        "Could not release input for handle (%p)", (void *) handle);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_forward(hg_core_handle_t handle, hg_core_cb_t callback, void *arg,
    uint8_t flags, hg_size_t payload_size)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc, handle == HG_CORE_HANDLE_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core handle");
    HG_CHECK_SUBSYS_ERROR(rpc, handle->info.addr == HG_CORE_ADDR_NULL, error,
        ret, HG_INVALID_ARG, "NULL target addr");
    HG_CHECK_SUBSYS_ERROR(
        rpc, handle->info.id == 0, error, ret, HG_INVALID_ARG, "NULL RPC ID");

    HG_LOG_SUBSYS_DEBUG(rpc, "Forwarding handle (%p), payload size is %" PRIu64,
        (void *) handle, payload_size);

    ret = hg_core_forward((struct hg_core_private_handle *) handle, callback,
        arg, flags, payload_size);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not forward handle (%p)", (void *) handle);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_respond(hg_core_handle_t handle, hg_core_cb_t callback, void *arg,
    uint8_t flags, hg_size_t payload_size)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc, handle == HG_CORE_HANDLE_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core handle");

    HG_LOG_SUBSYS_DEBUG(rpc,
        "Responding on handle (%p), payload size is %" PRIu64, (void *) handle,
        payload_size);

    /* Explicit response return code is always success here */
    ret = hg_core_respond((struct hg_core_private_handle *) handle, callback,
        arg, flags, payload_size, HG_SUCCESS);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not respond on handle (%p)", (void *) handle);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_cancel(hg_core_handle_t handle)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc, handle == HG_CORE_HANDLE_NULL, error, ret,
        HG_INVALID_ARG, "NULL HG core handle");

    HG_LOG_SUBSYS_DEBUG(rpc, "Canceling handle (%p)", (void *) handle);

    ret = hg_core_cancel((struct hg_core_private_handle *) handle);
    HG_CHECK_SUBSYS_HG_ERROR(
        rpc, error, ret, "Could not cancel handle (%p)", (void *) handle);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
#ifdef HG_HAS_MULTI_PROGRESS
hg_return_t
HG_Core_progress(hg_core_context_t *context, unsigned int timeout_ms)
{
    struct hg_core_private_context *hg_core_private_context =
        (struct hg_core_private_context *) context;
    struct hg_core_progress_multi *progress_multi = NULL;
    double remaining =
        timeout_ms / 1000.0; /* Convert timeout in ms into seconds */
    int32_t old, num;
    hg_return_t ret = HG_TIMEOUT;

    HG_CHECK_SUBSYS_ERROR(poll, context == NULL, done, ret, HG_INVALID_ARG,
        "NULL HG core context");
    progress_multi = &hg_core_private_context->progress_multi;

    hg_atomic_incr32(&progress_multi->count);
    for (;;) {
        hg_time_t t1, t2;

        old = hg_atomic_get32(&progress_multi->count) &
              (int32_t) ~HG_CORE_PROGRESS_LOCK;
        num = old | (int32_t) HG_CORE_PROGRESS_LOCK;
        if (hg_atomic_cas32(&progress_multi->count, old, num))
            break; /* No other thread is progressing */

        /* Timeout is 0 so leave */
        if (remaining <= 0) {
            hg_atomic_decr32(&progress_multi->count);
            goto done;
        }

        hg_time_get_current_ms(&t1);

        /* Prevent multiple threads from concurrently calling progress on
         * the same context */
        hg_thread_mutex_lock(&progress_multi->mutex);

        num = hg_atomic_get32(&progress_multi->count);
        /* Do not need to enter condition if lock is already released */
        if (((num & (int32_t) HG_CORE_PROGRESS_LOCK) != 0) &&
            (hg_thread_cond_timedwait(&progress_multi->cond,
                 &progress_multi->mutex,
                 (unsigned int) (remaining * 1000.0)) != HG_UTIL_SUCCESS)) {
            /* Timeout occurred so leave */
            hg_atomic_decr32(&progress_multi->count);
            hg_thread_mutex_unlock(&progress_multi->mutex);
            goto done;
        }

        hg_thread_mutex_unlock(&progress_multi->mutex);

        hg_time_get_current_ms(&t2);
        remaining -= hg_time_diff(t2, t1);
        /* Give a chance to call progress with timeout of 0 */
        if (remaining < 0)
            remaining = 0;
    }

    /* Make progress on the HG layer */
    ret = hg_core_progress_wait(
        hg_core_private_context, (unsigned int) (remaining * 1000.0));

    do {
        old = hg_atomic_get32(&progress_multi->count);
        num = (old - 1) ^ (int32_t) HG_CORE_PROGRESS_LOCK;
    } while (!hg_atomic_cas32(&progress_multi->count, old, num));

    if (num > 0) {
        /* If there is another processes entered in progress, signal it */
        hg_thread_mutex_lock(&progress_multi->mutex);
        hg_thread_cond_signal(&progress_multi->cond);
        hg_thread_mutex_unlock(&progress_multi->mutex);
    }

done:
    return ret;
}
#else
hg_return_t
HG_Core_progress(hg_core_context_t *context, unsigned int timeout)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(poll, context == NULL, done, ret, HG_INVALID_ARG,
        "NULL HG core context");

    /* Make progress on the HG layer */
    ret = hg_core_progress_wait(
        (struct hg_core_private_context *) context, timeout);
    HG_CHECK_SUBSYS_ERROR_NORET(poll, ret != HG_SUCCESS && ret != HG_TIMEOUT,
        done, "Could not make progress");

done:
    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_trigger(hg_core_context_t *context, unsigned int timeout,
    unsigned int max_count, unsigned int *actual_count_p)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(poll, context == NULL, done, ret, HG_INVALID_ARG,
        "NULL HG core context");

    ret = hg_core_trigger_wait((struct hg_core_private_context *) context,
        timeout, max_count, actual_count_p);
    HG_CHECK_SUBSYS_ERROR_NORET(poll, ret != HG_SUCCESS && ret != HG_TIMEOUT,
        done, "Could not trigger callbacks");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
int
HG_Core_event_get_wait_fd(const hg_core_context_t *context)
{
    const struct hg_core_private_context *private_context =
        (const struct hg_core_private_context *) context;

    HG_CHECK_SUBSYS_ERROR_NORET(
        poll, context == NULL, error, "NULL HG core context");

    if ((private_context->loopback_notify.event == 0)
#ifdef NA_HAS_SM
        && (private_context->core_context.na_sm_context == NULL)
#endif
    )
        return NA_Poll_get_fd(
            context->core_class->na_class, context->na_context);
    else if (private_context->poll_set != NULL)
        return hg_poll_get_fd(private_context->poll_set);
    else
        return -1;

error:
    return -1;
}

/*---------------------------------------------------------------------------*/
bool
HG_Core_event_ready(hg_core_context_t *context)
{
    struct hg_core_private_context *private_context =
        (struct hg_core_private_context *) context;

    HG_CHECK_SUBSYS_ERROR_NORET(
        poll, context == NULL, error, "NULL HG core context");

    if (hg_core_completion_count(private_context) > 0)
        return true;
#ifdef NA_HAS_SM
    if ((context->core_class->na_sm_class != NULL) &&
        !NA_Poll_try_wait(
            context->core_class->na_sm_class, context->na_sm_context))
        return true;
#endif
    if (!NA_Poll_try_wait(context->core_class->na_class, context->na_context))
        return true;
    return hg_core_event_ready_loopback(private_context);

error:
    return false;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_event_progress(hg_core_context_t *context, unsigned int *count_p)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(poll, context == NULL, error, ret, HG_INVALID_ARG,
        "NULL HG core context");

    ret = hg_core_progress((struct hg_core_private_context *) context, count_p);
    HG_CHECK_SUBSYS_HG_ERROR(
        poll, error, ret, "Could not progress context (%p)", (void *) context);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Core_event_trigger(hg_core_context_t *context, unsigned int max_count,
    unsigned int *actual_count_p)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(poll, context == NULL, error, ret, HG_INVALID_ARG,
        "NULL HG core context");

    hg_core_trigger(
        (struct hg_core_private_context *) context, max_count, actual_count_p);

    return HG_SUCCESS;

error:
    return ret;
}
