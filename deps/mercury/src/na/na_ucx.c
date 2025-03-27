/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_plugin.h"

#include "na_ip.h"

#include "mercury_hash_table.h"
#include "mercury_mem.h"
#include "mercury_mem_pool.h"
#include "mercury_thread_mutex.h"
#include "mercury_thread_rwlock.h"
#include "mercury_thread_spin.h"

#include <ucp/api/ucp.h>
#include <ucs/debug/log_def.h>
#include <uct/api/uct.h> /* To query component info */

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <sys/socket.h>

/****************/
/* Local Macros */
/****************/

/* Name of this class */
#define NA_UCX_CLASS_NAME "ucx"

/* Default protocol */
#define NA_UCX_PROTOCOL_DEFAULT "all"

/* Default features
 * (AM for unexpected messages and TAG for expected messages) */
#define NA_UCX_FEATURES (UCP_FEATURE_AM | UCP_FEATURE_TAG | UCP_FEATURE_RMA)

/* Default max msg size */
#define NA_UCX_MSG_SIZE_MAX (4096)

/* Address pool (enabled by default, comment out to disable) */
#define NA_UCX_HAS_ADDR_POOL
#define NA_UCX_ADDR_POOL_SIZE (64)

/* Memory pool (enabled by default, comment out to disable) */
#define NA_UCX_HAS_MEM_POOL
#define NA_UCX_MEM_CHUNK_COUNT (256)
#define NA_UCX_MEM_BLOCK_COUNT (2)

/* Addr status bits */
#define NA_UCX_ADDR_RESOLVED (1 << 0)

/* Max tag */
#define NA_UCX_MAX_TAG UINT32_MAX

/* Reserved tags */
#define NA_UCX_AM_MSG_ID (0)
#define NA_UCX_TAG_MASK  ((uint64_t) 0x00000000FFFFFFFF)

/* Op ID status bits */
#define NA_UCX_OP_COMPLETED (1 << 0)
#define NA_UCX_OP_CANCELING (1 << 1)
#define NA_UCX_OP_CANCELED  (1 << 2)
#define NA_UCX_OP_QUEUED    (1 << 3)
#define NA_UCX_OP_ERRORED   (1 << 4)

/* Private data access */
#define NA_UCX_CLASS(na_class)                                                 \
    ((struct na_ucx_class *) ((na_class)->plugin_class))
#define NA_UCX_CONTEXT(na_context)                                             \
    ((struct na_ucx_context *) ((na_context)->plugin_context))

/* Reset op ID */
#define NA_UCX_OP_RESET(_op, _context, _cb_type, _cb, _arg, _addr)             \
    do {                                                                       \
        _op->context = _context;                                               \
        _op->completion_data = (struct na_cb_completion_data){                 \
            .callback_info =                                                   \
                (struct na_cb_info){                                           \
                    .info.recv_unexpected =                                    \
                        (struct na_cb_info_recv_unexpected){                   \
                            .actual_buf_size = 0, .source = NULL, .tag = 0},   \
                    .arg = _arg,                                               \
                    .type = _cb_type,                                          \
                    .ret = NA_SUCCESS},                                        \
            .callback = _cb,                                                   \
            .plugin_callback = na_ucx_release,                                 \
            .plugin_callback_args = _op};                                      \
        _op->addr = _addr;                                                     \
        if (_addr)                                                             \
            na_ucx_addr_ref_incr(_addr);                                       \
        hg_atomic_set32(&_op->status, 0);                                      \
    } while (0)

#define NA_UCX_OP_RELEASE(_op)                                                 \
    do {                                                                       \
        if (_op->addr)                                                         \
            na_ucx_addr_ref_decr(_op->addr);                                   \
        hg_atomic_set32(&_op->status, NA_UCX_OP_COMPLETED);                    \
    } while (0)

#ifdef NA_HAS_DEBUG
#    define NA_UCX_PRINT_ADDR_KEY_INFO(_msg, _key)                             \
        do {                                                                   \
            char _host_string[NI_MAXHOST];                                     \
            char _serv_string[NI_MAXSERV];                                     \
                                                                               \
            (void) getnameinfo((_key)->addr, (_key)->addrlen, _host_string,    \
                sizeof(_host_string), _serv_string, sizeof(_serv_string),      \
                NI_NUMERICHOST | NI_NUMERICSERV);                              \
                                                                               \
            NA_LOG_SUBSYS_DEBUG(                                               \
                addr, _msg " (%s:%s)", _host_string, _serv_string);            \
        } while (0)
#else
#    define NA_UCX_PRINT_ADDR_KEY_INFO(_msg, _key) (void) 0
#endif

/************************************/
/* Local Type and Struct Definition */
/************************************/

/* Address */
struct na_ucx_addr {
    STAILQ_ENTRY(na_ucx_addr) entry;   /* Entry in addr pool */
    struct sockaddr_storage ss_addr;   /* Sock addr */
    ucs_sock_addr_t addr_key;          /* Address key */
    struct na_ucx_class *na_ucx_class; /* NA UCX class */
    ucp_address_t *worker_addr;        /* Worker addr */
    size_t worker_addr_len;            /* Worker addr len */
    bool worker_addr_alloc;            /* Worker addr was allocated by us */
    ucp_ep_h ucp_ep;                   /* Currently only one EP per address */
    hg_atomic_int32_t refcount;        /* Reference counter */
    hg_atomic_int32_t status;          /* Connection state */
};

/* Map (used to cache addresses) */
struct na_ucx_map {
    hg_thread_rwlock_t lock;
    hg_hash_table_t *key_map;
    hg_hash_table_t *ep_map;
};

/* Memory descriptor */
NA_PACKED(struct na_ucx_mem_desc {
    uint64_t base;          /* Base address */
    uint64_t len;           /* Size of region */
    uint64_t rkey_buf_size; /* Cached rkey buf size */
    uint8_t flags;          /* Flag of operation access */
});

/* Handle type */
enum na_ucx_mem_handle_type {
    NA_UCX_MEM_HANDLE_LOCAL,
    NA_UCX_MEM_HANDLE_REMOTE_PACKED,
    NA_UCX_MEM_HANDLE_REMOTE_UNPACKED
};

/* Memory handle */
struct na_ucx_mem_handle {
    struct na_ucx_mem_desc desc;        /* Memory descriptor */
    hg_thread_mutex_t rkey_unpack_lock; /* Unpack lock */
    union {
        ucp_mem_h mem;   /* UCP mem handle */
        ucp_rkey_h rkey; /* UCP rkey handle */
    } ucp_mr;
    void *rkey_buf;         /* Cached rkey buf */
    hg_atomic_int32_t type; /* Handle type (local / remote) */
};

/* Msg info */
struct na_ucx_msg_info {
    union {
        const void *const_ptr;
        void *ptr;
    } buf;
    size_t buf_size;
    ucp_tag_t tag;
};

/* UCP RMA op (put/get) */
typedef na_return_t (*na_ucp_rma_op_t)(ucp_ep_h ep, void *buf, size_t buf_size,
    uint64_t remote_addr, ucp_rkey_h rkey, void *request);

/* RMA info */
struct na_ucx_rma_info {
    na_ucp_rma_op_t ucp_rma_op;
    void *buf;
    size_t buf_size;
    uint64_t remote_addr;
    ucp_rkey_h remote_key;
};

/* Operation ID */
struct na_ucx_op_id {
    struct na_cb_completion_data completion_data; /* Completion data    */
    union {
        struct na_ucx_msg_info msg;
        struct na_ucx_rma_info rma;
    } info;                          /* Op info                  */
    TAILQ_ENTRY(na_ucx_op_id) entry; /* Entry in queue           */
    na_context_t *context;           /* NA context associated    */
    struct na_ucx_addr *addr;        /* Address associated       */
    hg_atomic_int32_t status;        /* Operation status         */
};

/* Addr pool */
struct na_ucx_addr_pool {
    STAILQ_HEAD(, na_ucx_addr) queue;
    hg_thread_spin_t lock;
};

/* Unexpected msg info */
struct na_ucx_unexpected_info {
    STAILQ_ENTRY(na_ucx_unexpected_info) entry;
    struct na_ucx_addr *na_ucx_addr;
    void *data;
    size_t length;
    ucp_tag_t tag;
    bool data_alloc;
};

/* Msg queue */
struct na_ucx_unexpected_msg_queue {
    STAILQ_HEAD(, na_ucx_unexpected_info) queue;
    hg_thread_spin_t lock;
};

/* Op ID queue */
struct na_ucx_op_queue {
    TAILQ_HEAD(, na_ucx_op_id) queue;
    hg_thread_spin_t lock;
};

/* UCX class */
struct na_ucx_class {
    struct na_ucx_unexpected_msg_queue
        unexpected_msg_queue;                   /* Unexpected msg queue */
    struct na_ucx_map addr_map;                 /* Address map */
    struct na_ucx_op_queue unexpected_op_queue; /* Unexpected op queue */
    struct na_ucx_addr_pool addr_pool;          /* Addr pool */
    ucp_context_h ucp_context;                  /* UCP context */
    ucp_worker_h ucp_worker;                    /* Shared UCP worker */
    ucp_listener_h ucp_listener;   /* Listener handle if listening */
    struct na_ucx_addr *self_addr; /* Self address */
    struct hg_mem_pool *mem_pool;  /* Msg buf pool */
    size_t ucp_request_size;       /* Size of UCP requests */
    char *protocol_name;           /* Protocol used */
    size_t unexpected_size_max;    /* Max unexpected size */
    size_t expected_size_max;      /* Max expected size */
    hg_atomic_int32_t ncontexts;   /* Number of contexts */
    bool no_wait;                  /* Wait disabled */
};

/* Datatype used for printing info */
enum na_ucp_type { NA_UCP_CONFIG, NA_UCP_CONTEXT, NA_UCP_WORKER };

/********************/
/* Local Prototypes */
/********************/

/*---------------------------------------------------------------------------*/
/* NA UCP helpers                                                            */
/*---------------------------------------------------------------------------*/

/**
 * Import/close UCX log.
 */
static void
na_ucs_log_import(void) NA_CONSTRUCTOR;
static void
na_ucs_log_close(void) NA_DESTRUCTOR;

/**
 * Print UCX log.
 */
static ucs_log_func_rc_t
na_ucs_log_func(const char *file, unsigned line, const char *function,
    ucs_log_level_t level, const ucs_log_component_config_t *comp_conf,
    const char *message, va_list ap) NA_PRINTF(6, 0);

/**
 * Convert UCX log level to HG log level.
 */
static enum hg_log_level
na_ucs_log_level_to_hg(ucs_log_level_t level);

/**
 * Convert HG log level to UCX log level string.
 */
static const char *
na_ucs_log_level_to_string(enum hg_log_level level);

/**
 * Convert UCX status to NA return values.
 */
static na_return_t
na_ucs_status_to_na(ucs_status_t status);

/**
 * Resolves transport aliases.
 */
static na_return_t
na_uct_get_transport_alias(
    const char *protocol_name, char *alias, size_t alias_size);

/**
 * Query UCT component.
 */
static na_return_t
na_uct_component_query(uct_component_h component, const char *protocol_name,
    struct na_protocol_info **na_protocol_info_p);

/**
 * Query transport info from component.
 */
static na_return_t
na_uct_get_md_info(uct_component_h component, const char *md_name,
    const char *protocol_name, struct na_protocol_info **na_protocol_info_p);

/**
 * Print debug info.
 */
#ifdef NA_HAS_DEBUG
static char *
na_ucp_tostr(void *data, enum na_ucp_type datatype);
#endif

/**
 * Init config.
 */
static na_return_t
na_ucp_config_init(
    const char *tls, const char *net_devices, ucp_config_t **config_p);

/**
 * Release config.
 */
static void
na_ucp_config_release(ucp_config_t *config);

/**
 * Create context.
 */
static na_return_t
na_ucp_context_create(const ucp_config_t *config, bool no_wait,
    ucs_thread_mode_t thread_mode, ucp_context_h *context_p,
    size_t *request_size_p);

/**
 * Destroy context.
 */
static void
na_ucp_context_destroy(ucp_context_h context);

/**
 * Create worker.
 */
static na_return_t
na_ucp_worker_create(ucp_context_h context, ucs_thread_mode_t thread_mode,
    ucp_worker_h *worker_p);

/**
 * Destroy worker.
 */
static void
na_ucp_worker_destroy(ucp_worker_h worker);

/**
 * Retrieve worker address.
 */
static na_return_t
na_ucp_worker_get_address(
    ucp_worker_h worker, ucp_address_t **addr_p, size_t *addr_len_p);

/**
 * Set handler for receiving active messages.
 */
static na_return_t
na_ucp_set_am_handler(
    ucp_worker_h worker, ucp_am_recv_callback_t am_recv_cb, void *arg);

/**
 * Create listener.
 */
static na_return_t
na_ucp_listener_create(ucp_worker_h context, const struct sockaddr *addr,
    socklen_t addrlen, void *listener_arg, ucp_listener_h *listener_p,
    struct sockaddr_storage *listener_addr);

/**
 * Destroy listener.
 */
static void
na_ucp_listener_destroy(ucp_listener_h listener);

/**
 * Listener callback.
 */
static void
na_ucp_listener_conn_cb(ucp_conn_request_h conn_request, void *arg);

/**
 * Accept connection.
 */
static na_return_t
na_ucp_accept(ucp_worker_h worker, ucp_conn_request_h conn_request,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p);

/**
 * Establish connection.
 */
static na_return_t
na_ucp_connect(ucp_worker_h worker, const struct sockaddr *src_addr,
    const struct sockaddr *dst_addr, socklen_t addrlen,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p);

/**
 * Create endpoint to worker using worker address (unconnected).
 */
static na_return_t
na_ucp_connect_worker(ucp_worker_h worker, ucp_address_t *address,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p);

/**
 * Create endpoint.
 */
static na_return_t
na_ucp_ep_create(ucp_worker_h worker, ucp_ep_params_t *ep_params,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p);

/**
 * Error handler.
 */
static void
na_ucp_ep_error_cb(void *arg, ucp_ep_h ep, ucs_status_t status);

/**
 * Close endpoint.
 */
static void
na_ucp_ep_close(ucp_ep_h ep);

#ifndef NA_UCX_HAS_MEM_POOL
/**
 * Allocate and register memory.
 */
static void *
na_ucp_mem_alloc(ucp_context_h context, size_t len, ucp_mem_h *mem_p);

/**
 * Free memory.
 */
static na_return_t
na_ucp_mem_free(ucp_context_h context, ucp_mem_h mem);

#else
/**
 * Register memory buffer.
 */
static int
na_ucp_mem_buf_register(const void *buf, size_t len, unsigned long flags,
    void **handle_p, void *arg);

/**
 * Deregister memory buffer.
 */
static int
na_ucp_mem_buf_deregister(void *handle, void *arg);

#endif /* NA_UCX_HAS_MEM_POOL */

/**
 * Send active message.
 */
static na_return_t
na_ucp_am_send(ucp_ep_h ep, const void *buf, size_t buf_size,
    const ucp_tag_t *tag, void *request);

/**
 * Send active message callback.
 */
static void
na_ucp_am_send_cb(
    void *request, ucs_status_t status, void NA_UNUSED *user_data);

/**
 * Check if we received an AM or push the op to OP queue.
 */
static void
na_ucp_am_recv(
    struct na_ucx_class *na_ucx_class, struct na_ucx_op_id *na_ucx_op_id);

/**
 * Recv active message callback.
 */
static ucs_status_t
na_ucp_am_recv_cb(void *arg, const void *header, size_t header_length,
    void *data, size_t length, const ucp_am_recv_param_t *param);

/**
 * Send a msg.
 */
static na_return_t
na_ucp_msg_send(ucp_ep_h ep, const void *buf, size_t buf_size, ucp_tag_t tag,
    void *request);

/**
 * Send msg callback.
 */
static void
na_ucp_msg_send_cb(void *request, ucs_status_t status, void *user_data);

/**
 * Recv a msg.
 */
static na_return_t
na_ucp_msg_recv(ucp_worker_h worker, void *buf, size_t buf_size, ucp_tag_t tag,
    void *request);

/**
 * Recv msg callback.
 */
static void
na_ucp_msg_recv_cb(void *request, ucs_status_t status,
    const ucp_tag_recv_info_t *info, void *user_data);

/**
 * RMA put.
 */
static na_return_t
na_ucp_put(ucp_ep_h ep, void *buf, size_t buf_size, uint64_t remote_addr,
    ucp_rkey_h rkey, void *request);

/**
 * RMA get.
 */
static na_return_t
na_ucp_get(ucp_ep_h ep, void *buf, size_t buf_size, uint64_t remote_addr,
    ucp_rkey_h rkey, void *request);

/**
 * RMA callback.
 */
static void
na_ucp_rma_cb(void *request, ucs_status_t status, void *user_data);

/*---------------------------------------------------------------------------*/
/* NA UCX helpers                                                            */
/*---------------------------------------------------------------------------*/

/**
 * Allocate new UCX class.
 */
static struct na_ucx_class *
na_ucx_class_alloc(void);

/**
 * Free UCX class.
 */
static void
na_ucx_class_free(struct na_ucx_class *na_ucx_class);

/**
 * Parse hostname info.
 */
static na_return_t
na_ucx_parse_hostname_info(const char *hostname_info, const char *subnet_info,
    bool listen, char **net_device_p, struct sockaddr **sockaddr_p,
    socklen_t *addrlen_p);

/**
 * Hash address key.
 */
static NA_INLINE unsigned int
na_ucx_addr_key_hash(hg_hash_table_key_t key);

/**
 * Compare address keys.
 */
static NA_INLINE int
na_ucx_addr_key_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Lookup addr from addr_key.
 */
static NA_INLINE struct na_ucx_addr *
na_ucx_addr_map_lookup(
    struct na_ucx_map *na_ucx_map, ucs_sock_addr_t *addr_key);

/**
 * Insert new addr using addr_key (if it does not already exist).
 */
static na_return_t
na_ucx_addr_map_insert(struct na_ucx_class *na_ucx_class,
    struct na_ucx_map *na_ucx_map, ucs_sock_addr_t *addr_key,
    ucp_conn_request_h conn_request, struct na_ucx_addr **na_ucx_addr_p);

/**
 * Update addr with new EP information.
 */
static na_return_t
na_ucx_addr_map_update(struct na_ucx_class *na_ucx_class,
    struct na_ucx_map *na_ucx_map, struct na_ucx_addr *na_ucx_addr);

/**
 * Remove addr from map using addr_key.
 */
static na_return_t
na_ucx_addr_map_remove(
    struct na_ucx_map *na_ucx_map, ucs_sock_addr_t *addr_key);

/**
 * Hash connection ID.
 */
static NA_INLINE unsigned int
na_ucx_addr_ep_hash(hg_hash_table_key_t key);

/**
 * Compare connection IDs.
 */
static NA_INLINE int
na_ucx_addr_ep_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Lookup addr from connection ID.
 */
static NA_INLINE struct na_ucx_addr *
na_ucx_addr_ep_lookup(struct na_ucx_map *na_ucx_map, ucp_ep_h ep);

/**
 * Allocate empty address.
 */
static struct na_ucx_addr *
na_ucx_addr_alloc(struct na_ucx_class *na_ucx_class);

/**
 * Destroy address.
 */
static void
na_ucx_addr_destroy(struct na_ucx_addr *na_ucx_addr);

#ifdef NA_UCX_HAS_ADDR_POOL
/**
 * Retrieve address from pool.
 */
static struct na_ucx_addr *
na_ucx_addr_pool_get(struct na_ucx_class *na_ucx_class);
#endif

/**
 * Release address without destroying it.
 */
static void
na_ucx_addr_release(struct na_ucx_addr *na_ucx_addr);

/**
 * Reset address.
 */
static void
na_ucx_addr_reset(struct na_ucx_addr *na_ucx_addr, ucs_sock_addr_t *addr_key);

/**
 * Create address.
 */
static na_return_t
na_ucx_addr_create(struct na_ucx_class *na_ucx_class, ucs_sock_addr_t *addr_key,
    struct na_ucx_addr **na_ucx_addr_p);

/**
 * Increment ref count.
 */
static NA_INLINE void
na_ucx_addr_ref_incr(struct na_ucx_addr *na_ucx_addr);

/**
 * Decrement ref count and free address if 0.
 */
static NA_INLINE void
na_ucx_addr_ref_decr(struct na_ucx_addr *na_ucx_addr);

/**
 * Allocate unexpected info.
 */
static struct na_ucx_unexpected_info *
na_ucx_unexpected_info_alloc(void *data, size_t data_alloc_size);

/**
 * Free unexpected info.
 */
static void
na_ucx_unexpected_info_free(
    struct na_ucx_unexpected_info *na_ucx_unexpected_info);

/**
 * Post RMA operation.
 */
static na_return_t
na_ucx_rma(struct na_ucx_class *na_ucx_class, na_context_t *context,
    na_cb_type_t cb_type, na_cb_t callback, void *arg,
    struct na_ucx_mem_handle *local_mem_handle, na_offset_t local_offset,
    struct na_ucx_mem_handle *remote_mem_handle, na_offset_t remote_offset,
    size_t length, struct na_ucx_addr *na_ucx_addr,
    struct na_ucx_op_id *na_ucx_op_id);

/**
 * Resolve RMA remote key.
 */
static na_return_t
na_ucx_rma_key_resolve(ucp_ep_h ep, struct na_ucx_mem_handle *na_ucx_mem_handle,
    ucp_rkey_h *rkey_p);

/**
 * Complete UCX operation.
 */
static NA_INLINE void
na_ucx_complete(struct na_ucx_op_id *na_ucx_op_id, na_return_t cb_ret);

/**
 * Release resources after NA callback execution.
 */
static NA_INLINE void
na_ucx_release(void *arg);

/********************/
/* Plugin callbacks */
/********************/

/* get_protocol_info */
static na_return_t
na_ucx_get_protocol_info(const struct na_info *na_info,
    struct na_protocol_info **na_protocol_info_p);

/* check_protocol */
static bool
na_ucx_check_protocol(const char *protocol_name);

/* initialize */
static na_return_t
na_ucx_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen);

/* finalize */
static na_return_t
na_ucx_finalize(na_class_t *na_class);

/* op_create */
static na_op_id_t *
na_ucx_op_create(na_class_t *na_class, unsigned long flags);

/* op_destroy */
static void
na_ucx_op_destroy(na_class_t *na_class, na_op_id_t *op_id);

/* addr_lookup */
static na_return_t
na_ucx_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr_p);

/* addr_free */
static NA_INLINE void
na_ucx_addr_free(na_class_t *na_class, na_addr_t *addr);

/* addr_self */
static NA_INLINE na_return_t
na_ucx_addr_self(na_class_t *na_class, na_addr_t **addr_p);

/* addr_dup */
static NA_INLINE na_return_t
na_ucx_addr_dup(na_class_t *na_class, na_addr_t *addr, na_addr_t **new_addr_p);

/* addr_dup */
static bool
na_ucx_addr_cmp(na_class_t *na_class, na_addr_t *addr1, na_addr_t *addr2);

/* addr_is_self */
static NA_INLINE bool
na_ucx_addr_is_self(na_class_t *na_class, na_addr_t *addr);

/* addr_to_string */
static na_return_t
na_ucx_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr);

/* addr_get_serialize_size */
static NA_INLINE size_t
na_ucx_addr_get_serialize_size(na_class_t *na_class, na_addr_t *addr);

/* addr_serialize */
static na_return_t
na_ucx_addr_serialize(
    na_class_t *na_class, void *buf, size_t buf_size, na_addr_t *addr);

/* addr_deserialize */
static na_return_t
na_ucx_addr_deserialize(
    na_class_t *na_class, na_addr_t **addr_p, const void *buf, size_t buf_size);

/* msg_get_max_unexpected_size */
static NA_INLINE size_t
na_ucx_msg_get_max_unexpected_size(const na_class_t *na_class);

/* msg_get_max_expected_size */
static NA_INLINE size_t
na_ucx_msg_get_max_expected_size(const na_class_t *na_class);

/* msg_get_max_tag */
static NA_INLINE na_tag_t
na_ucx_msg_get_max_tag(const na_class_t *na_class);

/* msg_buf_alloc */
static void *
na_ucx_msg_buf_alloc(na_class_t *na_class, size_t size, unsigned long flags,
    void **plugin_data_p);

/* msg_buf_free */
static void
na_ucx_msg_buf_free(na_class_t *na_class, void *buf, void *plugin_data);

/* msg_send_unexpected */
static na_return_t
na_ucx_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_unexpected */
static na_return_t
na_ucx_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id);

/* msg_send_expected */
static na_return_t
na_ucx_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_expected */
static na_return_t
na_ucx_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_addr_t *source_addr, uint8_t source_id, na_tag_t tag, na_op_id_t *op_id);

/* mem_handle */
static na_return_t
na_ucx_mem_handle_create(na_class_t *na_class, void *buf, size_t buf_size,
    unsigned long flags, na_mem_handle_t **mem_handle_p);

static void
na_ucx_mem_handle_free(na_class_t *na_class, na_mem_handle_t *mem_handle);

static NA_INLINE size_t
na_ucx_mem_handle_get_max_segments(const na_class_t *na_class);

static na_return_t
na_ucx_mem_register(na_class_t *na_class, na_mem_handle_t *mem_handle,
    enum na_mem_type mem_type, uint64_t device);

static na_return_t
na_ucx_mem_deregister(na_class_t *na_class, na_mem_handle_t *mem_handle);

/* mem_handle serialization */
static NA_INLINE size_t
na_ucx_mem_handle_get_serialize_size(
    na_class_t *na_class, na_mem_handle_t *mem_handle);

static na_return_t
na_ucx_mem_handle_serialize(na_class_t *na_class, void *buf, size_t buf_size,
    na_mem_handle_t *mem_handle);

static na_return_t
na_ucx_mem_handle_deserialize(na_class_t *na_class,
    na_mem_handle_t **mem_handle_p, const void *buf, size_t buf_size);

/* put */
static na_return_t
na_ucx_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* get */
static na_return_t
na_ucx_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* poll_get_fd */
static int
na_ucx_poll_get_fd(na_class_t *na_class, na_context_t *context);

/* poll_try_wait */
static NA_INLINE bool
na_ucx_poll_try_wait(na_class_t *na_class, na_context_t *context);

/* poll_wait */
static NA_INLINE na_return_t
na_ucx_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p);

/* cancel */
static na_return_t
na_ucx_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t *op_id);

/*******************/
/* Local Variables */
/*******************/

NA_PLUGIN const struct na_class_ops NA_PLUGIN_OPS(ucx) = {
    "ucx",                                /* name */
    na_ucx_get_protocol_info,             /* get_protocol_info */
    na_ucx_check_protocol,                /* check_protocol */
    na_ucx_initialize,                    /* initialize */
    na_ucx_finalize,                      /* finalize */
    NULL,                                 /* cleanup */
    NULL,                                 /* has_opt_feature */
    NULL,                                 /* context_create */
    NULL,                                 /* context_destroy */
    na_ucx_op_create,                     /* op_create */
    na_ucx_op_destroy,                    /* op_destroy */
    na_ucx_addr_lookup,                   /* addr_lookup */
    na_ucx_addr_free,                     /* addr_free */
    NULL,                                 /* addr_set_remove */
    na_ucx_addr_self,                     /* addr_self */
    na_ucx_addr_dup,                      /* addr_dup */
    na_ucx_addr_cmp,                      /* addr_cmp */
    na_ucx_addr_is_self,                  /* addr_is_self */
    na_ucx_addr_to_string,                /* addr_to_string */
    na_ucx_addr_get_serialize_size,       /* addr_get_serialize_size */
    na_ucx_addr_serialize,                /* addr_serialize */
    na_ucx_addr_deserialize,              /* addr_deserialize */
    na_ucx_msg_get_max_unexpected_size,   /* msg_get_max_unexpected_size */
    na_ucx_msg_get_max_expected_size,     /* msg_get_max_expected_size */
    NULL,                                 /* msg_get_unexpected_header_size */
    NULL,                                 /* msg_get_expected_header_size */
    na_ucx_msg_get_max_tag,               /* msg_get_max_tag */
    na_ucx_msg_buf_alloc,                 /* msg_buf_alloc */
    na_ucx_msg_buf_free,                  /* msg_buf_free */
    NULL,                                 /* msg_init_unexpected */
    na_ucx_msg_send_unexpected,           /* msg_send_unexpected */
    na_ucx_msg_recv_unexpected,           /* msg_recv_unexpected */
    NULL,                                 /* msg_multi_recv_unexpected */
    NULL,                                 /* msg_init_expected */
    na_ucx_msg_send_expected,             /* msg_send_expected */
    na_ucx_msg_recv_expected,             /* msg_recv_expected */
    na_ucx_mem_handle_create,             /* mem_handle_create */
    NULL,                                 /* mem_handle_create_segment */
    na_ucx_mem_handle_free,               /* mem_handle_free */
    na_ucx_mem_handle_get_max_segments,   /* mem_handle_get_max_segments */
    na_ucx_mem_register,                  /* mem_register */
    na_ucx_mem_deregister,                /* mem_deregister */
    na_ucx_mem_handle_get_serialize_size, /* mem_handle_get_serialize_size */
    na_ucx_mem_handle_serialize,          /* mem_handle_serialize */
    na_ucx_mem_handle_deserialize,        /* mem_handle_deserialize */
    na_ucx_put,                           /* put */
    na_ucx_get,                           /* get */
    na_ucx_poll_get_fd,                   /* poll_get_fd */
    na_ucx_poll_try_wait,                 /* poll_try_wait */
    na_ucx_poll,                          /* poll */
    NULL,                                 /* poll_wait */
    na_ucx_cancel                         /* cancel */
};

/* Thread mode names */
#ifndef NA_UCX_HAS_THREAD_MODE_NAMES
#    define NA_UCX_THREAD_MODES                                                \
        X(UCS_THREAD_MODE_SINGLE, "single")                                    \
        X(UCS_THREAD_MODE_SERIALIZED, "serialized")                            \
        X(UCS_THREAD_MODE_MULTI, "multi")
#    define X(a, b) b,
static const char *ucs_thread_mode_names[UCS_THREAD_MODE_LAST] = {
    NA_UCX_THREAD_MODES};
#    undef X
#endif

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucs_status_to_na(ucs_status_t status)
{
    na_return_t ret;

    switch (status) {
        case UCS_OK:
        case UCS_INPROGRESS:
            ret = NA_SUCCESS;
            break;

        case UCS_ERR_NO_ELEM:
            ret = NA_NOENTRY;
            break;

        case UCS_ERR_NO_PROGRESS:
            ret = NA_AGAIN;
            break;

        case UCS_ERR_NO_MEMORY:
            ret = NA_NOMEM;
            break;

        case UCS_ERR_BUSY:
            ret = NA_BUSY;
            break;

        case UCS_ERR_ALREADY_EXISTS:
            ret = NA_EXIST;
            break;

        case UCS_ERR_NO_RESOURCE:
        case UCS_ERR_NO_DEVICE:
            ret = NA_NODEV;
            break;

        case UCS_ERR_INVALID_PARAM:
            ret = NA_INVALID_ARG;
            break;

        case UCS_ERR_BUFFER_TOO_SMALL:
        case UCS_ERR_EXCEEDS_LIMIT:
        case UCS_ERR_OUT_OF_RANGE:
            ret = NA_OVERFLOW;
            break;

        case UCS_ERR_MESSAGE_TRUNCATED:
            ret = NA_MSGSIZE;
            break;

        case UCS_ERR_NOT_IMPLEMENTED:
            ret = NA_PROTONOSUPPORT;
            break;

        case UCS_ERR_UNSUPPORTED:
            ret = NA_OPNOTSUPPORTED;
            break;

        case UCS_ERR_INVALID_ADDR:
            ret = NA_ADDRNOTAVAIL;
            break;

        case UCS_ERR_UNREACHABLE:
        case UCS_ERR_CONNECTION_RESET:
        case UCS_ERR_NOT_CONNECTED:
        case UCS_ERR_REJECTED:
            ret = NA_HOSTUNREACH;
            break;

        case UCS_ERR_TIMED_OUT:
        case UCS_ERR_ENDPOINT_TIMEOUT:
            ret = NA_TIMEOUT;
            break;

        case UCS_ERR_CANCELED:
            ret = NA_CANCELED;
            break;

        case UCS_ERR_SOME_CONNECTS_FAILED:
        case UCS_ERR_IO_ERROR:
            ret = NA_IO_ERROR;
            break;

        case UCS_ERR_NO_MESSAGE:
        case UCS_ERR_SHMEM_SEGMENT:
        default:
            ret = NA_PROTOCOL_ERROR;
            break;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucs_log_import(void)
{
    ucs_log_push_handler(na_ucs_log_func);
}

/*---------------------------------------------------------------------------*/
static void
na_ucs_log_close(void)
{
    ucs_log_pop_handler();
}

/*---------------------------------------------------------------------------*/
static ucs_log_func_rc_t
na_ucs_log_func(const char *file, unsigned line, const char *function,
    ucs_log_level_t level, const ucs_log_component_config_t *comp_conf,
    const char *message, va_list ap)
{
    HG_LOG_VWRITE_FUNC(na_ucx, na_ucs_log_level_to_hg(level), comp_conf->name,
        file, line, function, false, message, ap);

    return UCS_LOG_FUNC_RC_STOP;
}

/*---------------------------------------------------------------------------*/
static enum hg_log_level
na_ucs_log_level_to_hg(ucs_log_level_t level)
{
    switch (level) {
        case UCS_LOG_LEVEL_FATAL:
        case UCS_LOG_LEVEL_ERROR:
            return HG_LOG_LEVEL_ERROR;
        case UCS_LOG_LEVEL_WARN:
            return HG_LOG_LEVEL_WARNING;
        case UCS_LOG_LEVEL_DIAG:
        case UCS_LOG_LEVEL_INFO:
        case UCS_LOG_LEVEL_DEBUG:
        case UCS_LOG_LEVEL_TRACE:
        case UCS_LOG_LEVEL_TRACE_REQ:
        case UCS_LOG_LEVEL_TRACE_DATA:
        case UCS_LOG_LEVEL_TRACE_ASYNC:
        case UCS_LOG_LEVEL_TRACE_FUNC:
        case UCS_LOG_LEVEL_TRACE_POLL:
            return HG_LOG_LEVEL_DEBUG;
        case UCS_LOG_LEVEL_LAST:
        case UCS_LOG_LEVEL_PRINT:
        default:
            return HG_LOG_LEVEL_MAX;
    }
}

/*---------------------------------------------------------------------------*/
static const char *
na_ucs_log_level_to_string(enum hg_log_level level)
{
    switch (level) {
        case HG_LOG_LEVEL_ERROR:
            return "error";
        case HG_LOG_LEVEL_WARNING:
            return "warn";
        case HG_LOG_LEVEL_MIN_DEBUG:
            return "trace";
        case HG_LOG_LEVEL_DEBUG:
            return "debug";
        case HG_LOG_LEVEL_NONE:
        case HG_LOG_LEVEL_MAX:
        default:
            return "";
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_uct_get_transport_alias(
    const char *protocol_name, char *tl_name, size_t tl_name_size)
{
    char *delim;
    size_t protocol_name_len = strlen(protocol_name);
    na_return_t ret;

    delim = strstr(protocol_name, "_");
    NA_CHECK_SUBSYS_ERROR(cls, delim == NULL, error, ret, NA_PROTONOSUPPORT,
        "No _ delimiter was found in %s", protocol_name);

    /* more than one character, no alias needed, copy entire string */
    if (strlen(delim + 1) > 1) {
        NA_CHECK_SUBSYS_ERROR(cls, protocol_name_len >= tl_name_size, error,
            ret, NA_OVERFLOW,
            "Length of protocol_name (%zu) exceeds tl_name_size (%zu)",
            protocol_name_len, tl_name_size);
        strcpy(tl_name, protocol_name);
    } else {
        const char *suffix = NULL;
        size_t delim_len = (size_t) (delim - protocol_name);
        size_t suffix_len;

        switch (delim[1]) {
            case 'x':
                suffix = "_mlx5";
                break;
            case 'v':
                suffix = "_verbs";
                break;
            default:
                NA_GOTO_SUBSYS_ERROR(cls, error, ret, NA_PROTONOSUPPORT,
                    "invalid protocol name (%s)", protocol_name);
        }
        suffix_len = strlen(suffix);

        NA_CHECK_SUBSYS_ERROR(cls, delim_len + suffix_len >= tl_name_size,
            error, ret, NA_OVERFLOW,
            "Length of transport alias (%zu) exceeds tl_name_size (%zu)",
            delim_len + suffix_len, tl_name_size);
        strncpy(tl_name, protocol_name, delim_len);
        tl_name[delim_len] = '\0';
        strcat(tl_name, suffix);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_uct_component_query(uct_component_h component, const char *protocol_name,
    struct na_protocol_info **na_protocol_info_p)
{
    uct_component_attr_t component_attr = {
        .field_mask = UCT_COMPONENT_ATTR_FIELD_NAME |
                      UCT_COMPONENT_ATTR_FIELD_MD_RESOURCE_COUNT |
                      UCT_COMPONENT_ATTR_FIELD_FLAGS};
    unsigned int i;
    ucs_status_t status;
    na_return_t ret;

    status = uct_component_query(component, &component_attr);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "uct_component_query() failed (%s)",
        ucs_status_string(status));

    component_attr.field_mask = UCT_COMPONENT_ATTR_FIELD_MD_RESOURCES;
    component_attr.md_resources = alloca(sizeof(*component_attr.md_resources) *
                                         component_attr.md_resource_count);

    status = uct_component_query(component, &component_attr);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "uct_component_query() failed (%s)",
        ucs_status_string(status));

    for (i = 0; i < component_attr.md_resource_count; i++) {
        ret = na_uct_get_md_info(component,
            component_attr.md_resources[i].md_name, protocol_name,
            na_protocol_info_p);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not get resource info");
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_uct_get_md_info(uct_component_h component, const char *md_name,
    const char *protocol_name, struct na_protocol_info **na_protocol_info_p)
{
    uct_md_config_t *md_config;
    uct_md_h md = NULL;
    uct_tl_resource_desc_t *resources = NULL;
    unsigned int num_resources, i;
    ucs_status_t status;
    na_return_t ret;

    status = uct_md_config_read(component, NULL, NULL, &md_config);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "uct_md_config_read() failed (%s)",
        ucs_status_string(status));

    status = uct_md_open(component, md_name, md_config, &md);
    uct_config_release(md_config);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "uct_md_open() failed (%s)",
        ucs_status_string(status));

    status = uct_md_query_tl_resources(md, &resources, &num_resources);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "uct_md_query_tl_resources() failed (%s)",
        ucs_status_string(status));

    for (i = 0; i < num_resources; i++) {
        struct na_protocol_info *entry;

        /* Skip non net resources (e.g., memory) */
        if (resources[i].dev_type != UCT_DEVICE_TYPE_NET)
            continue;

        if (protocol_name != NULL) {
            NA_LOG_SUBSYS_DEBUG(cls, "protocol_name=%s, tl_name=%s",
                protocol_name, resources[i].tl_name);

            if (strncmp(
                    protocol_name, resources[i].tl_name, strlen(protocol_name)))
                continue;
        }

        entry = na_protocol_info_alloc(
            NA_UCX_CLASS_NAME, resources[i].tl_name, resources[i].dev_name);
        NA_CHECK_SUBSYS_ERROR(cls, entry == NULL, error, ret, NA_NOMEM,
            "Could not allocate protocol info entry");

        entry->next = *na_protocol_info_p;
        *na_protocol_info_p = entry;
    }

    uct_release_tl_resource_list(resources);
    uct_md_close(md);

    return NA_SUCCESS;

error:
    if (md != NULL)
        uct_md_close(md);
    return ret;
}

/*---------------------------------------------------------------------------*/
#ifdef NA_HAS_DEBUG
static char *
na_ucp_tostr(void *data, enum na_ucp_type datatype)
{
    static char buf[4096];
    FILE *stream = NULL;

    stream = fmemopen(buf, sizeof(buf), "w");
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, stream == NULL, error, "fmemopen() failed");

    switch (datatype) {
        case NA_UCP_CONFIG:
            ucp_config_print((ucp_config_t *) data, stream, "UCX variables",
                UCS_CONFIG_PRINT_CONFIG | UCS_CONFIG_PRINT_HEADER);
            break;
        case NA_UCP_CONTEXT:
            ucp_context_print_info((ucp_context_h) data, stream);
            break;
        case NA_UCP_WORKER:
            ucp_worker_print_info((ucp_worker_h) data, stream);
            break;
        default:
            break;
    }

    fclose(stream);

    return buf;

error:
    return NULL;
}
#endif

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_config_init(
    const char *tls, const char *net_devices, ucp_config_t **config_p)
{
    ucp_config_t *config = NULL;
    ucs_status_t status;
    na_return_t ret;

    /* Read UCP configuration */
    status = ucp_config_read(NULL, NULL, &config);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_config_read() failed (%s)",
        ucs_status_string(status));

    /* Set user-requested transport */
    status = ucp_config_modify(config, "TLS", tls);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_config_modify() failed (%s)",
        ucs_status_string(status));

    /* Disable backtrace by default */
    if (getenv("UCX_HANDLE_ERRORS") == NULL) {
        status = ucp_config_modify(config, "HANDLE_ERRORS", "none");
        NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
            na_ucs_status_to_na(status), "ucp_config_modify() failed (%s)",
            ucs_status_string(status));
    }

    /* Set matching log level by default */
    if (getenv("UCX_LOG_LEVEL") == NULL) {
        status = ucp_config_modify(config, "LOG_LEVEL",
            na_ucs_log_level_to_string(hg_log_get_level()));
        NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
            na_ucs_status_to_na(status), "ucp_config_modify() failed (%s)",
            ucs_status_string(status));
    }

    /* Reuse addr for tcp by default */
    if (getenv("UCX_CM_REUSEADDR") == NULL) {
        status = ucp_config_modify(config, "CM_REUSEADDR", "y");
        NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
            na_ucs_status_to_na(status), "ucp_config_modify() failed (%s)",
            ucs_status_string(status));
    }

    /* Set network devices to use */
    if (net_devices) {
        status = ucp_config_modify(config, "NET_DEVICES", net_devices);
        NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
            na_ucs_status_to_na(status), "ucp_config_modify() failed (%s)",
            ucs_status_string(status));
    } else
        NA_LOG_SUBSYS_DEBUG(
            cls, "Could not find NET_DEVICE to use, using default");

    /* Print UCX config */
    NA_LOG_SUBSYS_DEBUG_EXT(cls,
        "Now using the following UCX global configuration", "%s",
        na_ucp_tostr(config, NA_UCP_CONFIG));

    *config_p = config;

    return NA_SUCCESS;

error:
    if (config)
        ucp_config_release(config);

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_config_release(ucp_config_t *config)
{
    ucp_config_release(config);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_context_create(const ucp_config_t *config, bool no_wait,
    ucs_thread_mode_t thread_mode, ucp_context_h *context_p,
    size_t *request_size_p)
{
    ucp_context_h context = NULL;
    ucp_params_t context_params = {
        .field_mask = UCP_PARAM_FIELD_FEATURES, .features = NA_UCX_FEATURES};
    ucp_context_attr_t context_attrs = {
        .field_mask = UCP_ATTR_FIELD_REQUEST_SIZE | UCP_ATTR_FIELD_THREAD_MODE};
    ucs_status_t status;
    na_return_t ret;

    /* Skip wakeup feature if not waiting */
    if (no_wait != true)
        context_params.features |= UCP_FEATURE_WAKEUP;

    if (thread_mode == UCS_THREAD_MODE_MULTI) {
        /* If the UCP context can potentially be used by more than one
         * worker / thread, then this context needs thread safety. */
        context_params.field_mask |= UCP_PARAM_FIELD_MT_WORKERS_SHARED;
        context_params.mt_workers_shared = 1;
    }

    /* Create UCP context */
    status = ucp_init(&context_params, config, &context);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_init() failed (%s)",
        ucs_status_string(status));

    /* Print context info */
    NA_LOG_SUBSYS_DEBUG_EXT(
        cls, "Context info", "%s", na_ucp_tostr(context, NA_UCP_CONTEXT));

    /* Query context to ensure we got what we asked for */
    status = ucp_context_query(context, &context_attrs);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_context_query() failed (%s)",
        ucs_status_string(status));

    /* Check that expected fields are present */
    NA_CHECK_SUBSYS_ERROR(cls,
        (context_attrs.field_mask & UCP_ATTR_FIELD_REQUEST_SIZE) == 0, error,
        ret, NA_PROTONOSUPPORT, "context attributes contain no request size");
    NA_CHECK_SUBSYS_ERROR(cls,
        (context_attrs.field_mask & UCP_ATTR_FIELD_THREAD_MODE) == 0, error,
        ret, NA_PROTONOSUPPORT, "context attributes contain no thread mode");

    /* Do not continue if thread mode is less than expected */
    NA_CHECK_SUBSYS_ERROR(cls,
        thread_mode != UCS_THREAD_MODE_SINGLE &&
            context_attrs.thread_mode < thread_mode,
        error, ret, NA_PROTONOSUPPORT, "Context thread mode is: %s",
        ucs_thread_mode_names[context_attrs.thread_mode]);

    NA_LOG_SUBSYS_DEBUG(
        cls, "UCP request size is %zu", context_attrs.request_size);

    *context_p = context;
    *request_size_p = context_attrs.request_size;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_context_destroy(ucp_context_h context)
{
    ucp_cleanup(context);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_worker_create(ucp_context_h context, ucs_thread_mode_t thread_mode,
    ucp_worker_h *worker_p)
{
    ucp_worker_h worker = NULL;
    ucp_worker_params_t worker_params = {
        .field_mask = UCP_WORKER_PARAM_FIELD_THREAD_MODE,
        .thread_mode = thread_mode};
    ucp_worker_attr_t worker_attrs = {
        .field_mask = UCP_WORKER_ATTR_FIELD_THREAD_MODE |
                      UCP_WORKER_ATTR_FIELD_MAX_AM_HEADER};
    ucs_status_t status;
    na_return_t ret;

    /* Create UCP worker */
    status = ucp_worker_create(context, &worker_params, &worker);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_worker_create() failed (%s)",
        ucs_status_string(status));

    /* Print worker info */
    NA_LOG_SUBSYS_DEBUG_EXT(
        ctx, "Worker info", "%s", na_ucp_tostr(worker, NA_UCP_WORKER));

    /* Query worker attributes */
    status = ucp_worker_query(worker, &worker_attrs);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_worker_query() failed (%s)",
        ucs_status_string(status));

    /* Check max AM header size */
    NA_CHECK_SUBSYS_ERROR(cls,
        (worker_attrs.field_mask & UCP_WORKER_ATTR_FIELD_MAX_AM_HEADER) == 0,
        error, ret, NA_PROTONOSUPPORT,
        "worker attributes contain no max AM header");
    NA_CHECK_SUBSYS_ERROR(cls, worker_attrs.max_am_header < sizeof(ucp_tag_t),
        error, ret, NA_PROTONOSUPPORT,
        "insufficient AM header size (expected %zu, got %zu)",
        sizeof(ucp_tag_t), worker_attrs.max_am_header);

    /* Check thread mode */
    NA_CHECK_SUBSYS_ERROR(cls,
        (worker_attrs.field_mask & UCP_WORKER_ATTR_FIELD_THREAD_MODE) == 0,
        error, ret, NA_PROTONOSUPPORT,
        "worker attributes contain no thread mode");
    NA_CHECK_SUBSYS_ERROR(cls,
        thread_mode != UCS_THREAD_MODE_SINGLE &&
            worker_attrs.thread_mode < thread_mode,
        error, ret, NA_PROTONOSUPPORT,
        "UCP worker thread mode (%s) is not supported",
        ucs_thread_mode_names[worker_attrs.thread_mode]);

    *worker_p = worker;

    return NA_SUCCESS;

error:
    if (worker)
        ucp_worker_destroy(worker);

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_worker_destroy(ucp_worker_h worker)
{
    ucp_worker_destroy(worker);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_worker_get_address(
    ucp_worker_h worker, ucp_address_t **addr_p, size_t *addr_len_p)
{
    ucs_status_t status;
    na_return_t ret = NA_SUCCESS;

    status = ucp_worker_get_address(worker, addr_p, addr_len_p);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, done, ret,
        na_ucs_status_to_na(status), "ucp_worker_get_address() failed (%s)",
        ucs_status_string(status));

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_set_am_handler(
    ucp_worker_h worker, ucp_am_recv_callback_t am_recv_cb, void *arg)
{
    ucp_am_handler_param_t param;
    ucs_status_t status;
    na_return_t ret = NA_SUCCESS;

    param.field_mask =
        UCP_AM_HANDLER_PARAM_FIELD_ID | UCP_AM_HANDLER_PARAM_FIELD_CB |
        UCP_AM_HANDLER_PARAM_FIELD_ARG | UCP_AM_HANDLER_PARAM_FIELD_FLAGS;
    param.id = NA_UCX_AM_MSG_ID;
    param.flags = UCP_AM_FLAG_WHOLE_MSG;
    param.cb = am_recv_cb;
    param.arg = arg;

    status = ucp_worker_set_am_recv_handler(worker, &param);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, done, ret,
        na_ucs_status_to_na(status),
        "ucp_worker_set_am_recv_handler() failed (%s)",
        ucs_status_string(status));

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_listener_create(ucp_worker_h worker, const struct sockaddr *addr,
    socklen_t addrlen, void *listener_arg, ucp_listener_h *listener_p,
    struct sockaddr_storage *listener_addr)
{
    ucp_listener_h listener = NULL;
    ucp_listener_params_t listener_params = {
        .field_mask = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
                      UCP_LISTENER_PARAM_FIELD_CONN_HANDLER,
        .sockaddr = (ucs_sock_addr_t){.addr = addr, .addrlen = addrlen},
        .conn_handler = (ucp_listener_conn_handler_t){
            .cb = na_ucp_listener_conn_cb, .arg = listener_arg}};
    ucp_listener_attr_t listener_attrs = {
        .field_mask = UCP_LISTENER_ATTR_FIELD_SOCKADDR};
    ucs_status_t status;
    na_return_t ret;

    /* Create listener on worker */
    status = ucp_listener_create(worker, &listener_params, &listener);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_listener_create() failed (%s)",
        ucs_status_string(status));

    /* Check sockaddr */
    status = ucp_listener_query(listener, &listener_attrs);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_listener_query() failed (%s)",
        ucs_status_string(status));

    NA_CHECK_SUBSYS_ERROR(cls,
        (listener_attrs.field_mask & UCP_LISTENER_ATTR_FIELD_SOCKADDR) == 0,
        error, ret, NA_PROTONOSUPPORT,
        "listener attributes contain no sockaddr");

    *listener_p = listener;
    memcpy(listener_addr, &listener_attrs.sockaddr, sizeof(*listener_addr));

    return NA_SUCCESS;

error:
    if (listener)
        ucp_listener_destroy(listener);
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_listener_destroy(ucp_listener_h listener)
{
    ucp_listener_destroy(listener);
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_listener_conn_cb(ucp_conn_request_h conn_request, void *arg)
{
    struct na_ucx_class *na_ucx_class = (struct na_ucx_class *) arg;
    ucp_conn_request_attr_t conn_request_attrs = {
        .field_mask = UCP_CONN_REQUEST_ATTR_FIELD_CLIENT_ADDR};
    struct na_ucx_addr *na_ucx_addr = NULL;
    ucs_sock_addr_t addr_key;
    ucs_status_t status;
    na_return_t na_ret;

    status = ucp_conn_request_query(conn_request, &conn_request_attrs);
    NA_CHECK_SUBSYS_ERROR_NORET(addr, status != UCS_OK, error,
        "ucp_conn_request_query() failed (%s)", ucs_status_string(status));

    NA_CHECK_SUBSYS_ERROR_NORET(addr,
        (conn_request_attrs.field_mask &
            UCP_CONN_REQUEST_ATTR_FIELD_CLIENT_ADDR) == 0,
        error, "conn attributes contain no client addr");

    /* Lookup address from table */
    addr_key = (ucs_sock_addr_t){
        .addr = (const struct sockaddr *) &conn_request_attrs.client_address,
        .addrlen = sizeof(conn_request_attrs.client_address)};
    na_ucx_addr = na_ucx_addr_map_lookup(&na_ucx_class->addr_map, &addr_key);
    NA_CHECK_SUBSYS_ERROR_NORET(addr, na_ucx_addr != NULL, error,
        "An entry is already present for this address");

    /* Insert new entry and create new address */
    na_ret = na_ucx_addr_map_insert(na_ucx_class, &na_ucx_class->addr_map,
        &addr_key, conn_request, &na_ucx_addr);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, na_ret, "Could not insert new address");

    return;

error:
    return;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_accept(ucp_worker_h worker, ucp_conn_request_h conn_request,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p)
{
    ucp_ep_params_t ep_params = {.field_mask = UCP_EP_PARAM_FIELD_CONN_REQUEST,
        .conn_request = conn_request};

    return na_ucp_ep_create(
        worker, &ep_params, err_handler_cb, err_handler_arg, ep_p);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_connect(ucp_worker_h worker, const struct sockaddr *src_addr,
    const struct sockaddr *dst_addr, socklen_t addrlen,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p)
{
    ucp_ep_params_t ep_params = {
        .field_mask = UCP_EP_PARAM_FIELD_FLAGS | UCP_EP_PARAM_FIELD_SOCK_ADDR,
        .flags = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER,
        .sockaddr = (ucs_sock_addr_t){.addr = dst_addr, .addrlen = addrlen},
        .conn_request = NULL};
    struct sockaddr_storage src_ss_addr;
    na_return_t ret;

#ifdef NA_UCX_HAS_FIELD_LOCAL_SOCK_ADDR
    if (src_addr != NULL) {
        /* Reset port to 0 to ensure a separate port is used per connection. */
        memcpy(&src_ss_addr, src_addr, addrlen);
        if (src_ss_addr.ss_family == AF_INET)
            ((struct sockaddr_in *) &src_ss_addr)->sin_port = 0;
        else if (src_ss_addr.ss_family == AF_INET6)
            ((struct sockaddr_in6 *) &src_ss_addr)->sin6_port = 0;
        else
            NA_GOTO_SUBSYS_ERROR(addr, error, ret, NA_PROTONOSUPPORT,
                "unsupported address family");

        ep_params.field_mask |= UCP_EP_PARAM_FIELD_LOCAL_SOCK_ADDR;
        ep_params.local_sockaddr.addr = (const struct sockaddr *) &src_ss_addr;
        ep_params.local_sockaddr.addrlen = addrlen;
    }
#else
    (void) src_addr;
    (void) src_ss_addr;
    (void) ret;
#endif

    return na_ucp_ep_create(
        worker, &ep_params, err_handler_cb, err_handler_arg, ep_p);

#ifdef NA_UCX_HAS_FIELD_LOCAL_SOCK_ADDR
error:
    return ret;
#endif
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_connect_worker(ucp_worker_h worker, ucp_address_t *address,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p)
{
    ucp_ep_params_t ep_params = {
        .field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS,
        .address = address,
        .conn_request = NULL};

    NA_LOG_SUBSYS_DEBUG(addr, "Connecting to worker ");

    return na_ucp_ep_create(
        worker, &ep_params, err_handler_cb, err_handler_arg, ep_p);
}

/*---------------------------------------------------------------------------*/
#ifndef NA_UCX_HAS_MEM_POOL
static void *
na_ucp_mem_alloc(ucp_context_h context, size_t len, ucp_mem_h *mem_p)
{
    const ucp_mem_map_params_t mem_map_params = {
        .field_mask =
            UCP_MEM_MAP_PARAM_FIELD_LENGTH | UCP_MEM_MAP_PARAM_FIELD_FLAGS,
        .length = len,
        .flags = UCP_MEM_MAP_ALLOCATE // TODO use UCP_MEM_MAP_NONBLOCK ?
    };
    ucp_mem_attr_t mem_attrs = {.field_mask = UCP_MEM_ATTR_FIELD_ADDRESS};
    ucp_mem_h mem = NULL;
    ucs_status_t status;

    /* Register memory */
    status = ucp_mem_map(context, &mem_map_params, &mem);
    NA_CHECK_SUBSYS_ERROR_NORET(mem, status != UCS_OK, error,
        "ucp_mem_map() failed (%s)", ucs_status_string(status));

    /* Query memory address */
    status = ucp_mem_query(mem, &mem_attrs);
    NA_CHECK_SUBSYS_ERROR_NORET(mem, status != UCS_OK, error,
        "ucp_mem_map() failed (%s)", ucs_status_string(status));
    NA_CHECK_SUBSYS_ERROR_NORET(mem,
        (mem_attrs.field_mask & UCP_MEM_ATTR_FIELD_ADDRESS) == 0, error,
        "mem attributes contain no address");

    *mem_p = mem;

    return mem_attrs.address;

error:
    if (mem)
        (void) ucp_mem_unmap(context, mem);
    return NULL;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_mem_free(ucp_context_h context, ucp_mem_h mem)
{
    ucs_status_t status;
    na_return_t ret;

    status = ucp_mem_unmap(context, mem);
    NA_CHECK_SUBSYS_ERROR(mem, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_mem_unmap() failed (%s)",
        ucs_status_string(status));

    return NA_SUCCESS;

error:
    return ret;
}

#else
/*---------------------------------------------------------------------------*/
static int
na_ucp_mem_buf_register(const void *buf, size_t len,
    unsigned long NA_UNUSED flags, void **handle_p, void *arg)
{
    struct na_ucx_class *na_ucx_class = (struct na_ucx_class *) arg;
    union {
        void *ptr;
        const void *const_ptr;
    } safe_buf = {.const_ptr = buf};
    const ucp_mem_map_params_t mem_map_params = {
        .field_mask =
            UCP_MEM_MAP_PARAM_FIELD_ADDRESS | UCP_MEM_MAP_PARAM_FIELD_LENGTH,
        .address = safe_buf.ptr,
        .length = len};
    ucs_status_t status;
    int ret;

    /* Register memory */
    status = ucp_mem_map(
        na_ucx_class->ucp_context, &mem_map_params, (ucp_mem_h *) handle_p);
    NA_CHECK_SUBSYS_ERROR(mem, status != UCS_OK, error, ret, HG_UTIL_FAIL,
        "ucp_mem_map() failed (%s)", ucs_status_string(status));

    return HG_UTIL_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static int
na_ucp_mem_buf_deregister(void *handle, void *arg)
{
    int ret;

    if (handle) {
        struct na_ucx_class *na_ucx_class = (struct na_ucx_class *) arg;
        ucp_mem_h mem = (ucp_mem_h) handle;
        ucs_status_t status;

        status = ucp_mem_unmap(na_ucx_class->ucp_context, mem);
        NA_CHECK_SUBSYS_ERROR(mem, status != UCS_OK, error, ret, HG_UTIL_FAIL,
            "ucp_mem_unmap() failed (%s)", ucs_status_string(status));
    }

    return HG_UTIL_SUCCESS;

error:
    return ret;
}

#endif /* NA_UCX_HAS_MEM_POOL */

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_ep_create(ucp_worker_h worker, ucp_ep_params_t *ep_params,
    ucp_err_handler_cb_t err_handler_cb, void *err_handler_arg, ucp_ep_h *ep_p)
{
    ucp_ep_h ep = NULL;
    ucs_status_t status;
    na_return_t ret;

    ep_params->field_mask |=
        UCP_EP_PARAM_FIELD_ERR_HANDLER | UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE;
    if (!(ep_params->field_mask & UCP_EP_PARAM_FIELD_REMOTE_ADDRESS))
        ep_params->err_mode = UCP_ERR_HANDLING_MODE_PEER;
    ep_params->err_handler.cb = err_handler_cb;
    ep_params->err_handler.arg = err_handler_arg;

    status = ucp_ep_create(worker, ep_params, &ep);
    NA_CHECK_SUBSYS_ERROR(addr, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_ep_create() failed (%s)",
        ucs_status_string(status));

    *ep_p = ep;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_ep_error_cb(
    void *arg, ucp_ep_h NA_UNUSED ep, ucs_status_t NA_DEBUG_LOG_USED status)
{
    struct na_ucx_addr *na_ucx_addr = (struct na_ucx_addr *) arg;

    NA_LOG_SUBSYS_DEBUG(addr, "ep_err_handler() returned (%s) for address (%p)",
        ucs_status_string(status), (void *) na_ucx_addr);

    /* Mark addr as no longer resolved to force reconnection */
    hg_atomic_and32(&na_ucx_addr->status, ~NA_UCX_ADDR_RESOLVED);

    /* Will schedule removal of address */
    na_ucx_addr_ref_decr(na_ucx_addr);
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_ep_close(ucp_ep_h ep)
{
    ucs_status_ptr_t status_ptr = ucp_ep_close_nb(ep, UCP_EP_CLOSE_MODE_FORCE);
    NA_CHECK_SUBSYS_ERROR_DONE(addr,
        status_ptr != NULL && UCS_PTR_IS_ERR(status_ptr),
        "ucp_ep_close_nb() failed (%s)",
        ucs_status_string(UCS_PTR_STATUS(status_ptr)));
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_am_send(ucp_ep_h ep, const void *buf, size_t buf_size,
    const ucp_tag_t *tag, void *request)
{
    const ucp_request_param_t send_params = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_REQUEST | UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_FLAGS,
        .cb = {.send = na_ucp_am_send_cb},
        .flags = UCP_AM_SEND_FLAG_REPLY,
        .request = request};
    ucs_status_ptr_t status_ptr;
    na_return_t ret;

    NA_LOG_SUBSYS_DEBUG(
        msg, "Posting am send with buf_size=%zu, tag=%" PRIu64, buf_size, *tag);

    status_ptr = ucp_am_send_nbx(
        ep, NA_UCX_AM_MSG_ID, tag, sizeof(*tag), buf, buf_size, &send_params);
    if (status_ptr == NULL) {
        /* Check for immediate completion */
        NA_LOG_SUBSYS_DEBUG(msg, "ucp_am_send_nbx() completed immediately");

        /* Directly execute callback */
        na_ucp_am_send_cb(request, UCS_OK, NULL);
    } else
        NA_CHECK_SUBSYS_ERROR(msg, UCS_PTR_IS_ERR(status_ptr), error, ret,
            na_ucs_status_to_na(UCS_PTR_STATUS(status_ptr)),
            "ucp_am_send_nbx() failed (%s)",
            ucs_status_string(UCS_PTR_STATUS(status_ptr)));

    NA_LOG_SUBSYS_DEBUG(msg, "ucp_am_send_nbx() was posted");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_am_send_cb(void *request, ucs_status_t status, void NA_UNUSED *user_data)
{
    na_return_t cb_ret;

    NA_LOG_SUBSYS_DEBUG(
        msg, "ucp_am_send_nbx() completed (%s)", ucs_status_string(status));

    if (status == UCS_OK)
        NA_GOTO_DONE(done, cb_ret, NA_SUCCESS);
    if (status == UCS_ERR_CANCELED)
        NA_GOTO_DONE(done, cb_ret, NA_CANCELED);
    else
        NA_GOTO_SUBSYS_ERROR(msg, done, cb_ret, na_ucs_status_to_na(status),
            "ucp_am_send_nbx() failed (%s)", ucs_status_string(status));

done:
    na_ucx_complete((struct na_ucx_op_id *) request, cb_ret);
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_am_recv(
    struct na_ucx_class *na_ucx_class, struct na_ucx_op_id *na_ucx_op_id)
{
    struct na_ucx_unexpected_msg_queue *unexpected_msg_queue =
        &na_ucx_class->unexpected_msg_queue;
    struct na_ucx_unexpected_info *na_ucx_unexpected_info;

    /* Look for an unexpected message already received */
    hg_thread_spin_lock(&unexpected_msg_queue->lock);
    na_ucx_unexpected_info = STAILQ_FIRST(&unexpected_msg_queue->queue);
    if (na_ucx_unexpected_info != NULL)
        STAILQ_REMOVE_HEAD(&unexpected_msg_queue->queue, entry);
    hg_thread_spin_unlock(&unexpected_msg_queue->lock);

    if (likely(na_ucx_unexpected_info == NULL)) {
        struct na_ucx_op_queue *unexpected_op_queue =
            &na_ucx_class->unexpected_op_queue;

        /* Nothing has been received yet so add op_id to progress queue */
        hg_thread_spin_lock(&unexpected_op_queue->lock);
        TAILQ_INSERT_TAIL(&unexpected_op_queue->queue, na_ucx_op_id, entry);
        hg_atomic_or32(&na_ucx_op_id->status, NA_UCX_OP_QUEUED);
        hg_thread_spin_unlock(&unexpected_op_queue->lock);
    } else {
        NA_LOG_SUBSYS_DEBUG(msg, "Unexpected data was already received");

        /* Copy buffers */
        memcpy(na_ucx_op_id->info.msg.buf.ptr, na_ucx_unexpected_info->data,
            na_ucx_unexpected_info->length);

        /* Fill unexpected info */
        na_ucx_op_id->completion_data.callback_info.info.recv_unexpected =
            (struct na_cb_info_recv_unexpected){
                .tag = (na_tag_t) na_ucx_unexpected_info->tag,
                .actual_buf_size = (size_t) na_ucx_unexpected_info->length,
                .source = (na_addr_t *) na_ucx_unexpected_info->na_ucx_addr};

        /* Release AM buffer if returned UCS_INPROGRESS */
        if (!na_ucx_unexpected_info->data_alloc &&
            na_ucx_unexpected_info->length > 0) {
            ucp_am_data_release(
                na_ucx_class->ucp_worker, na_ucx_unexpected_info->data);
        }
        na_ucx_unexpected_info_free(na_ucx_unexpected_info);

        na_ucx_complete(na_ucx_op_id, NA_SUCCESS);
    }
}

/*---------------------------------------------------------------------------*/
static ucs_status_t
na_ucp_am_recv_cb(void *arg, const void *header, size_t header_length,
    void *data, size_t length, const ucp_am_recv_param_t *param)
{
    struct na_ucx_class *na_ucx_class = (struct na_ucx_class *) arg;
    struct na_ucx_op_queue *unexpected_op_queue =
        &na_ucx_class->unexpected_op_queue;
    struct na_ucx_op_id *na_ucx_op_id = NULL;
    struct na_ucx_addr *source_addr = NULL;
    ucp_tag_t tag;
    ucs_status_t ret;

    /* Retrieve tag */
    NA_CHECK_SUBSYS_ERROR(msg, header_length != sizeof(tag), error, ret,
        UCS_ERR_INVALID_PARAM, "Invalid tag size (%zu)", header_length);
    memcpy(&tag, header, sizeof(tag));

    NA_CHECK_SUBSYS_ERROR(msg,
        (param->recv_attr & UCP_AM_RECV_ATTR_FIELD_REPLY_EP) == 0, error, ret,
        UCS_ERR_INVALID_PARAM, "recv attributes contain no reply EP");
    NA_LOG_SUBSYS_DEBUG(msg,
        "ucp_am_recv() completed (tag=%" PRIu64 ", reply_ep=%p)", tag,
        (void *) param->reply_ep);

    /* Look up addr */
    source_addr =
        na_ucx_addr_ep_lookup(&na_ucx_class->addr_map, param->reply_ep);
    NA_CHECK_SUBSYS_ERROR(addr, source_addr == NULL, error, ret,
        UCS_ERR_INVALID_PARAM,
        "No entry found for previously inserted src addr");

    /* Pop op ID from queue */
    hg_thread_spin_lock(&unexpected_op_queue->lock);
    na_ucx_op_id = TAILQ_FIRST(&unexpected_op_queue->queue);
    if (likely(na_ucx_op_id)) {
        TAILQ_REMOVE(&unexpected_op_queue->queue, na_ucx_op_id, entry);
        hg_atomic_and32(&na_ucx_op_id->status, ~NA_UCX_OP_QUEUED);
    }
    hg_thread_spin_unlock(&unexpected_op_queue->lock);

    if (likely(na_ucx_op_id)) {
        /* Fill info */
        na_ucx_op_id->completion_data.callback_info.info.recv_unexpected =
            (struct na_cb_info_recv_unexpected){.tag = (na_tag_t) tag,
                .actual_buf_size = (size_t) length,
                .source = (na_addr_t *) source_addr};
        na_ucx_addr_ref_incr(source_addr);

        /* Copy buffer */
        memcpy(na_ucx_op_id->info.msg.buf.ptr, data, length);

        /* Complete operation */
        na_ucx_complete(na_ucx_op_id, NA_SUCCESS);

        return UCS_OK;
    } else {
        struct na_ucx_unexpected_msg_queue *unexpected_msg_queue =
            &na_ucx_class->unexpected_msg_queue;
        struct na_ucx_unexpected_info *na_ucx_unexpected_info = NULL;
        bool data_alloc = !(param->recv_attr & UCP_AM_RECV_ATTR_FLAG_DATA);

        NA_LOG_SUBSYS_WARNING(perf,
            "No operation was preposted, data will persist (data_alloc=%d)",
            (int) data_alloc);

        /* If no error and message arrived, keep a copy of the struct in
         * the unexpected message queue (should rarely happen) */
        na_ucx_unexpected_info =
            na_ucx_unexpected_info_alloc(data, data_alloc ? length : 0);
        NA_CHECK_SUBSYS_ERROR(msg, na_ucx_unexpected_info == NULL, error, ret,
            UCS_ERR_NO_MEMORY, "Could not allocate unexpected info");

        na_ucx_unexpected_info->length = length;
        na_ucx_unexpected_info->tag = tag;
        na_ucx_unexpected_info->na_ucx_addr = source_addr;
        na_ucx_addr_ref_incr(source_addr);

        /* Otherwise push the unexpected message into our unexpected queue so
         * that we can treat it later when a recv_unexpected is posted */
        hg_thread_spin_lock(&unexpected_msg_queue->lock);
        STAILQ_INSERT_TAIL(
            &unexpected_msg_queue->queue, na_ucx_unexpected_info, entry);
        hg_thread_spin_unlock(&unexpected_msg_queue->lock);

        /* If data is going to be used outside this callback, UCS_INPROGRESS
         * should be returned, otherwise return UCS_OK as a copy was made */
        return (data_alloc) ? UCS_OK : UCS_INPROGRESS;
    }

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_msg_send(
    ucp_ep_h ep, const void *buf, size_t buf_size, ucp_tag_t tag, void *request)
{
    const ucp_request_param_t send_params = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_REQUEST | UCP_OP_ATTR_FIELD_CALLBACK,
        .cb = {.send = na_ucp_msg_send_cb},
        .request = request};
    ucs_status_ptr_t status_ptr;
    na_return_t ret;

    NA_LOG_SUBSYS_DEBUG(
        msg, "Posting msg send with buf_size=%zu, tag=%" PRIu64, buf_size, tag);

    status_ptr = ucp_tag_send_nbx(ep, buf, buf_size, tag, &send_params);
    if (status_ptr == NULL) {
        /* Check for immediate completion */
        NA_LOG_SUBSYS_DEBUG(msg, "ucp_tag_send_nbx() completed immediately");

        /* Directly execute callback */
        na_ucp_msg_send_cb(request, UCS_OK, NULL);
    } else
        NA_CHECK_SUBSYS_ERROR(msg, UCS_PTR_IS_ERR(status_ptr), error, ret,
            na_ucs_status_to_na(UCS_PTR_STATUS(status_ptr)),
            "ucp_tag_send_nbx() failed (%s)",
            ucs_status_string(UCS_PTR_STATUS(status_ptr)));

    NA_LOG_SUBSYS_DEBUG(msg, "ucp_tag_send_nbx() was posted");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_msg_send_cb(
    void *request, ucs_status_t status, void NA_UNUSED *user_data)
{
    na_return_t cb_ret;

    NA_LOG_SUBSYS_DEBUG(
        msg, "ucp_tag_send_nbx() completed (%s)", ucs_status_string(status));

    if (status == UCS_OK)
        NA_GOTO_DONE(done, cb_ret, NA_SUCCESS);
    if (status == UCS_ERR_CANCELED)
        NA_GOTO_DONE(done, cb_ret, NA_CANCELED);
    else
        NA_GOTO_SUBSYS_ERROR(msg, done, cb_ret, na_ucs_status_to_na(status),
            "ucp_tag_send_nbx() failed (%s)", ucs_status_string(status));

done:
    na_ucx_complete((struct na_ucx_op_id *) request, cb_ret);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_msg_recv(ucp_worker_h worker, void *buf, size_t buf_size, ucp_tag_t tag,
    void *request)
{
    ucp_tag_recv_info_t tag_recv_info = {.length = 0, .sender_tag = 0};
    const ucp_request_param_t recv_params = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_REQUEST | UCP_OP_ATTR_FIELD_CALLBACK |
                        UCP_OP_ATTR_FIELD_RECV_INFO,
        .cb = {.recv = na_ucp_msg_recv_cb},
        .request = request,
        .recv_info.tag_info = &tag_recv_info};
    ucs_status_ptr_t status_ptr;
    na_return_t ret;

    NA_LOG_SUBSYS_DEBUG(
        msg, "Posting msg recv with buf_size=%zu, tag=%" PRIu64, buf_size, tag);

    status_ptr = ucp_tag_recv_nbx(
        worker, buf, buf_size, tag, NA_UCX_TAG_MASK, &recv_params);
    if (status_ptr == NULL) {
        /* Check for immediate completion */
        NA_LOG_SUBSYS_DEBUG(msg, "ucp_tag_recv_nbx() completed immediately");

        /* Directly execute callback */
        na_ucp_msg_recv_cb(request, UCS_OK, &tag_recv_info, NULL);
    } else
        NA_CHECK_SUBSYS_ERROR(msg, UCS_PTR_IS_ERR(status_ptr), error, ret,
            na_ucs_status_to_na(UCS_PTR_STATUS(status_ptr)),
            "ucp_tag_recv_nbx() failed (%s)",
            ucs_status_string(UCS_PTR_STATUS(status_ptr)));

    NA_LOG_SUBSYS_DEBUG(msg, "ucp_tag_recv_nbx() was posted");

    return NA_SUCCESS;

error:

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_msg_recv_cb(void *request, ucs_status_t status,
    const ucp_tag_recv_info_t *info, void NA_UNUSED *user_data)
{
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) request;
    na_cb_type_t cb_type = na_ucx_op_id->completion_data.callback_info.type;
    struct na_cb_info_recv_expected *recv_expected_info =
        &na_ucx_op_id->completion_data.callback_info.info.recv_expected;
    na_return_t cb_ret = NA_SUCCESS;

    NA_LOG_SUBSYS_DEBUG(
        msg, "ucp_tag_recv_nbx() completed (%s)", ucs_status_string(status));

    if (status == UCS_OK)
        cb_ret = NA_SUCCESS;
    else if (status == UCS_ERR_CANCELED)
        NA_GOTO_DONE(done, cb_ret, NA_CANCELED);
    else
        NA_GOTO_SUBSYS_ERROR(msg, done, cb_ret, na_ucs_status_to_na(status),
            "ucp_tag_recv_nbx() failed (%s)", ucs_status_string(status));

    NA_CHECK_SUBSYS_ERROR(msg,
        (info->sender_tag & NA_UCX_TAG_MASK) > NA_UCX_MAX_TAG, done, cb_ret,
        NA_OVERFLOW, "Invalid tag value %" PRIu64, info->sender_tag);
    NA_CHECK_SUBSYS_ERROR(msg, cb_type != NA_CB_RECV_EXPECTED, done, cb_ret,
        NA_INVALID_ARG, "Invalid cb_type %s, expected NA_CB_RECV_EXPECTED",
        na_cb_type_to_string(cb_type));

    NA_LOG_SUBSYS_DEBUG(msg, "Received msg length=%zu, sender_tag=%" PRIu64,
        info->length, info->sender_tag);

    /* Keep actual msg size */
    NA_CHECK_SUBSYS_ERROR(msg, info->length > na_ucx_op_id->info.msg.buf_size,
        done, cb_ret, NA_MSGSIZE,
        "Expected recv msg size too large for buffer");
    recv_expected_info->actual_buf_size = (size_t) info->length;

done:
    na_ucx_complete(na_ucx_op_id, cb_ret);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_put(ucp_ep_h ep, void *buf, size_t buf_size, uint64_t remote_addr,
    ucp_rkey_h rkey, void *request)
{
    const ucp_request_param_t rma_params = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_REQUEST,
        .cb = {.send = na_ucp_rma_cb},
        .request = request};
    ucs_status_ptr_t status_ptr;
    na_return_t ret;

    status_ptr = ucp_put_nbx(ep, buf, buf_size, remote_addr, rkey, &rma_params);
    if (status_ptr == NULL) {
        /* Check for immediate completion */
        NA_LOG_SUBSYS_DEBUG(rma, "ucp_put_nbx() completed immediately");

        /* Directly execute callback */
        na_ucp_rma_cb(request, UCS_OK, NULL);
    } else
        NA_CHECK_SUBSYS_ERROR(rma, UCS_PTR_IS_ERR(status_ptr), error, ret,
            na_ucs_status_to_na(UCS_PTR_STATUS(status_ptr)),
            "ucp_put_nbx() failed (%s)",
            ucs_status_string(UCS_PTR_STATUS(status_ptr)));

    NA_LOG_SUBSYS_DEBUG(rma, "ucp_put_nbx() was posted");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucp_get(ucp_ep_h ep, void *buf, size_t buf_size, uint64_t remote_addr,
    ucp_rkey_h rkey, void *request)
{
    const ucp_request_param_t rma_params = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_REQUEST,
        .cb = {.send = na_ucp_rma_cb},
        .request = request};
    ucs_status_ptr_t status_ptr;
    na_return_t ret;

    status_ptr = ucp_get_nbx(ep, buf, buf_size, remote_addr, rkey, &rma_params);
    if (status_ptr == NULL) {
        /* Check for immediate completion */
        NA_LOG_SUBSYS_DEBUG(rma, "ucp_get_nbx() completed immediately");

        /* Directly execute callback */
        na_ucp_rma_cb(request, UCS_OK, NULL);
    } else
        NA_CHECK_SUBSYS_ERROR(rma, UCS_PTR_IS_ERR(status_ptr), error, ret,
            na_ucs_status_to_na(UCS_PTR_STATUS(status_ptr)),
            "ucp_get_nbx() failed (%s)",
            ucs_status_string(UCS_PTR_STATUS(status_ptr)));

    NA_LOG_SUBSYS_DEBUG(rma, "ucp_get_nbx() was posted");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucp_rma_cb(void *request, ucs_status_t status, void NA_UNUSED *user_data)
{
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) request;
    na_return_t cb_ret;

    NA_LOG_SUBSYS_DEBUG(
        rma, "ucp_put/get_nbx() completed (%s)", ucs_status_string(status));

    if (status == UCS_OK)
        NA_GOTO_DONE(done, cb_ret, NA_SUCCESS);
    if (status == UCS_ERR_CANCELED)
        NA_GOTO_DONE(done, cb_ret, NA_CANCELED);
    else
        NA_GOTO_SUBSYS_ERROR(rma, done, cb_ret, na_ucs_status_to_na(status),
            "na_ucp_rma_cb() failed (%s)", ucs_status_string(status));

done:
    na_ucx_complete(na_ucx_op_id, cb_ret);
}

/*---------------------------------------------------------------------------*/
static struct na_ucx_class *
na_ucx_class_alloc(void)
{
    struct na_ucx_class *na_ucx_class = NULL;
    int rc;

    na_ucx_class = calloc(1, sizeof(*na_ucx_class));
    NA_CHECK_SUBSYS_ERROR_NORET(cls, na_ucx_class == NULL, error,
        "Could not allocate NA private data class");

    /* Init table lock */
    rc = hg_thread_rwlock_init(&na_ucx_class->addr_map.lock);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, rc != HG_UTIL_SUCCESS, error, "hg_thread_rwlock_init() failed");

    /* Initialize unexpected op queue */
    rc = hg_thread_spin_init(&na_ucx_class->unexpected_op_queue.lock);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, rc != HG_UTIL_SUCCESS, error, "hg_thread_spin_init() failed");
    TAILQ_INIT(&na_ucx_class->unexpected_op_queue.queue);

    /* Initialize unexpected msg queue */
    rc = hg_thread_spin_init(&na_ucx_class->unexpected_msg_queue.lock);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, rc != HG_UTIL_SUCCESS, error, "hg_thread_spin_init() failed");
    STAILQ_INIT(&na_ucx_class->unexpected_msg_queue.queue);

    /* Initialize addr pool */
    rc = hg_thread_spin_init(&na_ucx_class->addr_pool.lock);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, rc != HG_UTIL_SUCCESS, error, "hg_thread_spin_init() failed");
    STAILQ_INIT(&na_ucx_class->addr_pool.queue);

    /* Create address map */
    na_ucx_class->addr_map.key_map =
        hg_hash_table_new(na_ucx_addr_key_hash, na_ucx_addr_key_equal);
    NA_CHECK_SUBSYS_ERROR_NORET(cls, na_ucx_class->addr_map.key_map == NULL,
        error, "Could not allocate key map");

    /* Create connection map */
    na_ucx_class->addr_map.ep_map =
        hg_hash_table_new(na_ucx_addr_ep_hash, na_ucx_addr_ep_equal);
    NA_CHECK_SUBSYS_ERROR_NORET(cls, na_ucx_class->addr_map.ep_map == NULL,
        error, "Could not allocate EP handle map");

    return na_ucx_class;

error:
    if (na_ucx_class)
        na_ucx_class_free(na_ucx_class);

    return NULL;
}

/*---------------------------------------------------------------------------*/
static void
na_ucx_class_free(struct na_ucx_class *na_ucx_class)
{
#ifdef NA_UCX_HAS_MEM_POOL
    hg_mem_pool_destroy(na_ucx_class->mem_pool);
#endif

    if (na_ucx_class->self_addr)
        na_ucx_addr_destroy(na_ucx_class->self_addr);
    if (na_ucx_class->ucp_listener)
        na_ucp_listener_destroy(na_ucx_class->ucp_listener);
    if (na_ucx_class->ucp_worker)
        na_ucp_worker_destroy(na_ucx_class->ucp_worker);
    if (na_ucx_class->ucp_context)
        na_ucp_context_destroy(na_ucx_class->ucp_context);

    if (na_ucx_class->addr_map.key_map)
        hg_hash_table_free(na_ucx_class->addr_map.key_map);
    if (na_ucx_class->addr_map.ep_map)
        hg_hash_table_free(na_ucx_class->addr_map.ep_map);
    (void) hg_thread_rwlock_destroy(&na_ucx_class->addr_map.lock);

    (void) hg_thread_spin_destroy(&na_ucx_class->unexpected_op_queue.lock);
    (void) hg_thread_spin_destroy(&na_ucx_class->unexpected_msg_queue.lock);
    (void) hg_thread_spin_destroy(&na_ucx_class->addr_pool.lock);

    free(na_ucx_class->protocol_name);
    free(na_ucx_class);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_parse_hostname_info(const char *hostname_info, const char *subnet_info,
    bool listen, char **net_device_p, struct sockaddr **sockaddr_p,
    socklen_t *addrlen_p)
{
    char **ifa_name_p = NULL;
    char *hostname = NULL;
    uint16_t port = 0;
    na_return_t ret = NA_SUCCESS;

    /* Set hostname (use default interface name if no hostname was passed) */
    if (hostname_info) {
        hostname = strdup(hostname_info);
        NA_CHECK_SUBSYS_ERROR(cls, hostname == NULL, done, ret, NA_NOMEM,
            "strdup() of hostname failed");

        /* TODO add support for IPv6 address parsing */

        /* Extract net_device if explicitly listed with '/' before IP */
        if (strstr(hostname, "/")) {
            char *host_str = NULL, *tmp = hostname;

            strtok_r(hostname, "/", &host_str);
            if (strcmp(hostname, "") == 0)
                ifa_name_p = net_device_p;
            else {
                *net_device_p = strdup(hostname);
                NA_CHECK_SUBSYS_ERROR(cls, *net_device_p == NULL, done, ret,
                    NA_NOMEM, "strdup() of net_device failed");
            }
            if (strcmp(host_str, "") == 0)
                hostname = NULL;
            else {
                hostname = strdup(host_str);
                NA_CHECK_SUBSYS_ERROR(cls, hostname == NULL, done, ret,
                    NA_NOMEM, "strdup() of hostname failed");
            }
            free(tmp);
        } else
            ifa_name_p = net_device_p;

        /* Extract hostname : port */
        if (hostname && strstr(hostname, ":")) {
            char *port_str = NULL;
            strtok_r(hostname, ":", &port_str);
            port = strtoul(port_str, NULL, 10) & 0xffff;
            if (port != 0 && !listen) {
                NA_LOG_SUBSYS_WARNING(
                    cls, "Not listening, port value is ignored");
                port = 0;
            }
        }
    }

    /* TODO add support for IPv6 wildcards */

    if (hostname && strcmp(hostname, "0.0.0.0") != 0) {
        /* Try to get matching IP/device */
        ret = na_ip_check_interface(
            hostname, port, AF_UNSPEC, ifa_name_p, sockaddr_p, addrlen_p);
        NA_CHECK_SUBSYS_NA_ERROR(cls, done, ret, "Could not check interfaces");
    } else {
        char pref_anyip[NI_MAXHOST];
        uint32_t subnet = 0, netmask = 0;

        /* Try to use IP subnet */
        if (subnet_info) {
            ret = na_ip_parse_subnet(subnet_info, &subnet, &netmask);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, done, ret, "na_ip_parse_subnet() failed");
        }
        ret = na_ip_pref_addr(subnet, netmask, pref_anyip);
        NA_CHECK_SUBSYS_NA_ERROR(cls, done, ret, "na_ip_pref_addr() failed");

        /* Generate IP address (ignore net_device) */
        ret = na_ip_check_interface(
            pref_anyip, port, AF_INET, NULL, sockaddr_p, addrlen_p);
        NA_CHECK_SUBSYS_NA_ERROR(cls, done, ret, "Could not check interfaces");
    }

done:
    free(hostname);
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_ucx_addr_key_hash(hg_hash_table_key_t key)
{
    ucs_sock_addr_t *addr_key = (ucs_sock_addr_t *) key;

    if (addr_key->addr->sa_family == AF_INET)
        return (unsigned int) ((const struct sockaddr_in *) addr_key->addr)
            ->sin_addr.s_addr;
    else
        return (unsigned int) ((const struct sockaddr_in6 *) addr_key->addr)
            ->sin6_addr.__in6_u.__u6_addr32[0];
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ucx_addr_key_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    ucs_sock_addr_t *addr_key1 = (ucs_sock_addr_t *) key1,
                    *addr_key2 = (ucs_sock_addr_t *) key2;

    return (addr_key1->addrlen == addr_key2->addrlen) &&
           (memcmp(addr_key1->addr, addr_key2->addr, addr_key1->addrlen) == 0);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE struct na_ucx_addr *
na_ucx_addr_map_lookup(struct na_ucx_map *na_ucx_map, ucs_sock_addr_t *addr_key)
{
    hg_hash_table_value_t value = NULL;

    /* Lookup key */
    hg_thread_rwlock_rdlock(&na_ucx_map->lock);
    value = hg_hash_table_lookup(
        na_ucx_map->key_map, (hg_hash_table_key_t) addr_key);
    hg_thread_rwlock_release_rdlock(&na_ucx_map->lock);

    return (value == HG_HASH_TABLE_NULL) ? NULL : (struct na_ucx_addr *) value;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_map_insert(struct na_ucx_class *na_ucx_class,
    struct na_ucx_map *na_ucx_map, ucs_sock_addr_t *addr_key,
    ucp_conn_request_h conn_request, struct na_ucx_addr **na_ucx_addr_p)
{
    struct na_ucx_addr *na_ucx_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    hg_thread_rwlock_wrlock(&na_ucx_map->lock);

    /* Look up again to prevent race between lock release/acquire */
    na_ucx_addr = (struct na_ucx_addr *) hg_hash_table_lookup(
        na_ucx_map->key_map, (hg_hash_table_key_t) addr_key);
    if (na_ucx_addr) {
        ret = NA_EXIST; /* Entry already exists */
        goto done;
    }

    /* Allocate address */
    ret = na_ucx_addr_create(na_ucx_class, addr_key, &na_ucx_addr);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not allocate NA UCX addr");

    if (conn_request) {
        /* Accept connection */
        ret = na_ucp_accept(na_ucx_class->ucp_worker, conn_request,
            na_ucp_ep_error_cb, (void *) na_ucx_addr, &na_ucx_addr->ucp_ep);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, error, ret, "Could not accept connection request");
    } else {
        /* Create new endpoint */
        ret = na_ucp_connect(na_ucx_class->ucp_worker,
            na_ucx_class->self_addr->addr_key.addr, na_ucx_addr->addr_key.addr,
            na_ucx_addr->addr_key.addrlen, na_ucp_ep_error_cb,
            (void *) na_ucx_addr, &na_ucx_addr->ucp_ep);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, error, ret, "Could not connect UCP endpoint");
    }
    NA_LOG_SUBSYS_DEBUG(addr, "UCP ep for addr %p is %p", (void *) na_ucx_addr,
        (void *) na_ucx_addr->ucp_ep);

    /* Insert new value to secondary map to lookup by EP handle */
    rc = hg_hash_table_insert(na_ucx_map->ep_map,
        (hg_hash_table_key_t) na_ucx_addr->ucp_ep,
        (hg_hash_table_value_t) na_ucx_addr);
    NA_CHECK_SUBSYS_ERROR(
        addr, rc == 0, error, ret, NA_NOMEM, "hg_hash_table_insert() failed");

    /* Insert new value to primary map */
    rc = hg_hash_table_insert(na_ucx_map->key_map,
        (hg_hash_table_key_t) &na_ucx_addr->addr_key,
        (hg_hash_table_value_t) na_ucx_addr);
    NA_CHECK_SUBSYS_ERROR(
        addr, rc == 0, error, ret, NA_NOMEM, "hg_hash_table_insert() failed");

    hg_atomic_or32(&na_ucx_addr->status, NA_UCX_ADDR_RESOLVED);

done:
    hg_thread_rwlock_release_wrlock(&na_ucx_map->lock);

    *na_ucx_addr_p = na_ucx_addr;

    return ret;

error:
    hg_thread_rwlock_release_wrlock(&na_ucx_map->lock);
    if (na_ucx_addr)
        na_ucx_addr_destroy(na_ucx_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_map_update(struct na_ucx_class *na_ucx_class,
    struct na_ucx_map *na_ucx_map, struct na_ucx_addr *na_ucx_addr)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    hg_thread_rwlock_wrlock(&na_ucx_map->lock);

    /* Check again to prevent race between lock release/acquire */
    if (hg_atomic_get32(&na_ucx_addr->status) & NA_UCX_ADDR_RESOLVED)
        goto unlock;

    NA_LOG_SUBSYS_DEBUG(
        addr, "Attempting to reconnect addr %p", (void *) na_ucx_addr);

    /* Remove EP handle from secondary map */
    rc = hg_hash_table_remove(
        na_ucx_map->ep_map, (hg_hash_table_key_t) na_ucx_addr->ucp_ep);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, unlock, ret, NA_NOENTRY,
        "hg_hash_table_remove() failed");

    /* Close previous EP */
    na_ucp_ep_close(na_ucx_addr->ucp_ep);
    na_ucx_addr->ucp_ep = NULL;

    /* Create new endpoint */
    ret = na_ucp_connect(na_ucx_class->ucp_worker,
        na_ucx_class->self_addr->addr_key.addr, na_ucx_addr->addr_key.addr,
        na_ucx_addr->addr_key.addrlen, na_ucp_ep_error_cb, (void *) na_ucx_addr,
        &na_ucx_addr->ucp_ep);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, unlock, ret, "Could not connect UCP endpoint");

    NA_LOG_SUBSYS_DEBUG(addr, "UCP ep for addr %p is %p", (void *) na_ucx_addr,
        (void *) na_ucx_addr->ucp_ep);

    /* Insert new value to secondary map to lookup by EP handle */
    rc = hg_hash_table_insert(na_ucx_map->ep_map,
        (hg_hash_table_key_t) na_ucx_addr->ucp_ep,
        (hg_hash_table_value_t) na_ucx_addr);
    NA_CHECK_SUBSYS_ERROR(
        addr, rc == 0, unlock, ret, NA_NOMEM, "hg_hash_table_insert() failed");

    /* Retake refcount taken away from previous disconnect */
    na_ucx_addr_ref_incr(na_ucx_addr);

    hg_atomic_or32(&na_ucx_addr->status, NA_UCX_ADDR_RESOLVED);

unlock:
    hg_thread_rwlock_release_wrlock(&na_ucx_map->lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_map_remove(struct na_ucx_map *na_ucx_map, ucs_sock_addr_t *addr_key)
{
    struct na_ucx_addr *na_ucx_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    hg_thread_rwlock_wrlock(&na_ucx_map->lock);

    na_ucx_addr = hg_hash_table_lookup(
        na_ucx_map->key_map, (hg_hash_table_key_t) addr_key);
    if (na_ucx_addr == HG_HASH_TABLE_NULL)
        goto unlock;

    /* Remove addr key from primary map */
    rc = hg_hash_table_remove(
        na_ucx_map->key_map, (hg_hash_table_key_t) addr_key);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, unlock, ret, NA_NOENTRY,
        "hg_hash_table_remove() failed");

    /* Remove EP handle from secondary map */
    rc = hg_hash_table_remove(
        na_ucx_map->ep_map, (hg_hash_table_key_t) na_ucx_addr->ucp_ep);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, unlock, ret, NA_NOENTRY,
        "hg_hash_table_remove() failed");

unlock:
    hg_thread_rwlock_release_wrlock(&na_ucx_map->lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_ucx_addr_ep_hash(hg_hash_table_key_t key)
{
    uint64_t ep = (uint64_t) key;
    uint32_t hi, lo;

    hi = (uint32_t) (ep >> 32);
    lo = (ep & 0xFFFFFFFFU);

    return (hi & 0xFFFF0000U) | (lo & 0xFFFFU);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ucx_addr_ep_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    return (ucp_ep_h) key1 == (ucp_ep_h) key2;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE struct na_ucx_addr *
na_ucx_addr_ep_lookup(struct na_ucx_map *na_ucx_map, ucp_ep_h ep)
{
    hg_hash_table_value_t value = NULL;

    /* Lookup key */
    hg_thread_rwlock_rdlock(&na_ucx_map->lock);
    value = hg_hash_table_lookup(na_ucx_map->ep_map, (hg_hash_table_key_t) ep);
    hg_thread_rwlock_release_rdlock(&na_ucx_map->lock);

    return (value == HG_HASH_TABLE_NULL) ? NULL : (struct na_ucx_addr *) value;
}

/*---------------------------------------------------------------------------*/
static struct na_ucx_addr *
na_ucx_addr_alloc(struct na_ucx_class *na_ucx_class)
{
    struct na_ucx_addr *na_ucx_addr;

    na_ucx_addr = calloc(1, sizeof(*na_ucx_addr));
    if (na_ucx_addr)
        na_ucx_addr->na_ucx_class = na_ucx_class;

    return na_ucx_addr;
}

/*---------------------------------------------------------------------------*/
static void
na_ucx_addr_destroy(struct na_ucx_addr *na_ucx_addr)
{
    NA_LOG_SUBSYS_DEBUG(addr, "Destroying address %p", (void *) na_ucx_addr);

    na_ucx_addr_release(na_ucx_addr);
    free(na_ucx_addr);
}

/*---------------------------------------------------------------------------*/
#ifdef NA_UCX_HAS_ADDR_POOL
static struct na_ucx_addr *
na_ucx_addr_pool_get(struct na_ucx_class *na_ucx_class)
{
    struct na_ucx_addr *na_ucx_addr = NULL;

    hg_thread_spin_lock(&na_ucx_class->addr_pool.lock);
    na_ucx_addr = STAILQ_FIRST(&na_ucx_class->addr_pool.queue);
    if (na_ucx_addr) {
        STAILQ_REMOVE_HEAD(&na_ucx_class->addr_pool.queue, entry);
        hg_thread_spin_unlock(&na_ucx_class->addr_pool.lock);
    } else {
        hg_thread_spin_unlock(&na_ucx_class->addr_pool.lock);
        /* Fallback to allocation if pool is empty */
        na_ucx_addr = na_ucx_addr_alloc(na_ucx_class);
    }

    return na_ucx_addr;
}
#endif

/*---------------------------------------------------------------------------*/
static void
na_ucx_addr_release(struct na_ucx_addr *na_ucx_addr)
{
    /* Make sure we remove from map before we close the EP */
    if (na_ucx_addr->addr_key.addr) {
        NA_UCX_PRINT_ADDR_KEY_INFO("Removing address", &na_ucx_addr->addr_key);

        na_ucx_addr_map_remove(
            &na_ucx_addr->na_ucx_class->addr_map, &na_ucx_addr->addr_key);
    }

    if (na_ucx_addr->ucp_ep != NULL) {
        /* NB. for deserialized addresses that are not "connected" addresses, do
         * not close the EP */
        if (na_ucx_addr->worker_addr == NULL)
            na_ucp_ep_close(na_ucx_addr->ucp_ep);
        na_ucx_addr->ucp_ep = NULL;
    }

    if (na_ucx_addr->worker_addr != NULL) {
        if (na_ucx_addr->worker_addr_alloc)
            free(na_ucx_addr->worker_addr);
        else
            ucp_worker_release_address(na_ucx_addr->na_ucx_class->ucp_worker,
                na_ucx_addr->worker_addr);
        na_ucx_addr->worker_addr = NULL;
        na_ucx_addr->worker_addr_len = 0;
    }
}

/*---------------------------------------------------------------------------*/
static void
na_ucx_addr_reset(struct na_ucx_addr *na_ucx_addr, ucs_sock_addr_t *addr_key)
{
    na_ucx_addr->ucp_ep = NULL;
    hg_atomic_init32(&na_ucx_addr->refcount, 1);
    hg_atomic_init32(&na_ucx_addr->status, 0);

    if (addr_key && addr_key->addr) {
        memcpy(&na_ucx_addr->ss_addr, addr_key->addr, addr_key->addrlen);

        /* Point key back to ss_addr */
        na_ucx_addr->addr_key.addr =
            (const struct sockaddr *) &na_ucx_addr->ss_addr;
        na_ucx_addr->addr_key.addrlen = addr_key->addrlen;
    } else {
        memset(&na_ucx_addr->ss_addr, 0, sizeof(na_ucx_addr->ss_addr));
        na_ucx_addr->addr_key = (ucs_sock_addr_t){.addr = NULL, .addrlen = 0};
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_create(struct na_ucx_class *na_ucx_class, ucs_sock_addr_t *addr_key,
    struct na_ucx_addr **na_ucx_addr_p)
{
    struct na_ucx_addr *na_ucx_addr;
    na_return_t ret;

    if (addr_key != NULL) {
        NA_UCX_PRINT_ADDR_KEY_INFO("Creating new address", addr_key);
    }

#ifdef NA_UCX_HAS_ADDR_POOL
    na_ucx_addr = na_ucx_addr_pool_get(na_ucx_class);
#else
    na_ucx_addr = na_ucx_addr_alloc(na_ucx_class);
#endif
    NA_CHECK_SUBSYS_ERROR(addr, na_ucx_addr == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA UCX addr");

    na_ucx_addr_reset(na_ucx_addr, addr_key);
    NA_LOG_SUBSYS_DEBUG(addr, "Created address %p", (void *) na_ucx_addr);

    *na_ucx_addr_p = na_ucx_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ucx_addr_ref_incr(struct na_ucx_addr *na_ucx_addr)
{
    int32_t NA_DEBUG_LOG_USED refcount =
        hg_atomic_incr32(&na_ucx_addr->refcount);
    NA_LOG_SUBSYS_DEBUG(addr, "Refcount for address (%p) is: %" PRId32,
        (void *) na_ucx_addr, refcount);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ucx_addr_ref_decr(struct na_ucx_addr *na_ucx_addr)
{
    int32_t refcount = hg_atomic_decr32(&na_ucx_addr->refcount);
    NA_LOG_SUBSYS_DEBUG(addr, "Refcount for address (%p) is: %" PRId32,
        (void *) na_ucx_addr, refcount);

    if (refcount == 0) {
#ifdef NA_UCX_HAS_ADDR_POOL
        struct na_ucx_addr_pool *addr_pool =
            &na_ucx_addr->na_ucx_class->addr_pool;

        NA_LOG_SUBSYS_DEBUG(addr, "Releasing address %p", (void *) na_ucx_addr);
        na_ucx_addr_release(na_ucx_addr);

        /* Push address back to addr pool */
        hg_thread_spin_lock(&addr_pool->lock);
        STAILQ_INSERT_TAIL(&addr_pool->queue, na_ucx_addr, entry);
        hg_thread_spin_unlock(&addr_pool->lock);
#else
        na_ucx_addr_destroy(na_ucx_addr);
#endif
    }
}

/*---------------------------------------------------------------------------*/
static struct na_ucx_unexpected_info *
na_ucx_unexpected_info_alloc(void *data, size_t data_alloc_size)
{
    struct na_ucx_unexpected_info *na_ucx_unexpected_info;

    na_ucx_unexpected_info = (struct na_ucx_unexpected_info *) calloc(
        1, sizeof(*na_ucx_unexpected_info));
    NA_CHECK_SUBSYS_ERROR_NORET(msg, na_ucx_unexpected_info == NULL, error,
        "Could not allocate unexpected info");

    if (data_alloc_size > 0) {
        na_ucx_unexpected_info->data = malloc(data_alloc_size);
        NA_CHECK_SUBSYS_ERROR_NORET(msg, na_ucx_unexpected_info->data == NULL,
            error, "Could not allocate data of size %zu", data_alloc_size);
        na_ucx_unexpected_info->data_alloc = true;
        memcpy(na_ucx_unexpected_info->data, data, data_alloc_size);
    } else {
        na_ucx_unexpected_info->data = data;
        na_ucx_unexpected_info->data_alloc = false;
    }

    return na_ucx_unexpected_info;

error:
    free(na_ucx_unexpected_info);

    return NULL;
}

/*---------------------------------------------------------------------------*/
static void
na_ucx_unexpected_info_free(
    struct na_ucx_unexpected_info *na_ucx_unexpected_info)
{
    if (na_ucx_unexpected_info->data_alloc)
        free(na_ucx_unexpected_info->data);
    free(na_ucx_unexpected_info);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_rma(struct na_ucx_class NA_UNUSED *na_ucx_class, na_context_t *context,
    na_cb_type_t cb_type, na_cb_t callback, void *arg,
    struct na_ucx_mem_handle *local_mem_handle, na_offset_t local_offset,
    struct na_ucx_mem_handle *remote_mem_handle, na_offset_t remote_offset,
    size_t length, struct na_ucx_addr *na_ucx_addr,
    struct na_ucx_op_id *na_ucx_op_id)
{
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ucx_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ucx_op_id->completion_data.callback_info.type));

    NA_UCX_OP_RESET(na_ucx_op_id, context, cb_type, callback, arg, na_ucx_addr);

    na_ucx_op_id->info.rma.ucp_rma_op =
        (cb_type == NA_CB_PUT) ? na_ucp_put : na_ucp_get;
    na_ucx_op_id->info.rma.buf =
        (char *) local_mem_handle->desc.base + local_offset;
    na_ucx_op_id->info.rma.remote_addr =
        remote_mem_handle->desc.base + remote_offset;
    na_ucx_op_id->info.rma.buf_size = length;
    na_ucx_op_id->info.rma.remote_key = NULL;

    /* There is no need to have a fully resolved address to start an RMA.
     * This is only necessary for two-sided communication. */

    /* TODO UCX requires the remote key to be bound to the origin, do we need a
     * new API? */
    ret = na_ucx_rma_key_resolve(na_ucx_addr->ucp_ep, remote_mem_handle,
        &na_ucx_op_id->info.rma.remote_key);
    NA_CHECK_SUBSYS_NA_ERROR(rma, release, ret, "Could not resolve remote key");

    /* Post RMA op */
    ret = na_ucx_op_id->info.rma.ucp_rma_op(na_ucx_addr->ucp_ep,
        na_ucx_op_id->info.rma.buf, na_ucx_op_id->info.rma.buf_size,
        na_ucx_op_id->info.rma.remote_addr, na_ucx_op_id->info.rma.remote_key,
        na_ucx_op_id);
    NA_CHECK_SUBSYS_NA_ERROR(rma, release, ret, "Could not post rma operation");

    return NA_SUCCESS;

release:
    NA_UCX_OP_RELEASE(na_ucx_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_rma_key_resolve(ucp_ep_h ep, struct na_ucx_mem_handle *na_ucx_mem_handle,
    ucp_rkey_h *rkey_p)
{
    na_return_t ret;

    if (hg_atomic_get32(&na_ucx_mem_handle->type) ==
        NA_UCX_MEM_HANDLE_REMOTE_UNPACKED) {
        *rkey_p = na_ucx_mem_handle->ucp_mr.rkey;
        return NA_SUCCESS;
    }

    hg_thread_mutex_lock(&na_ucx_mem_handle->rkey_unpack_lock);

    switch (hg_atomic_get32(&na_ucx_mem_handle->type)) {
        case NA_UCX_MEM_HANDLE_REMOTE_PACKED: {
            ucs_status_t status = ucp_ep_rkey_unpack(ep,
                na_ucx_mem_handle->rkey_buf, &na_ucx_mem_handle->ucp_mr.rkey);
            NA_CHECK_SUBSYS_ERROR(mem, status != UCS_OK, error, ret,
                na_ucs_status_to_na(status), "ucp_ep_rkey_unpack() failed (%s)",
                ucs_status_string(status));
            /* Handle is now unpacked */
            hg_atomic_set32(
                &na_ucx_mem_handle->type, NA_UCX_MEM_HANDLE_REMOTE_UNPACKED);
            break;
        }
        case NA_UCX_MEM_HANDLE_REMOTE_UNPACKED:
            break;
        case NA_UCX_MEM_HANDLE_LOCAL:
        default:
            NA_GOTO_SUBSYS_ERROR(
                mem, error, ret, NA_INVALID_ARG, "Invalid memory handle type");
    }

    *rkey_p = na_ucx_mem_handle->ucp_mr.rkey;
    hg_thread_mutex_unlock(&na_ucx_mem_handle->rkey_unpack_lock);

    return NA_SUCCESS;

error:
    hg_thread_mutex_unlock(&na_ucx_mem_handle->rkey_unpack_lock);
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ucx_complete(struct na_ucx_op_id *na_ucx_op_id, na_return_t cb_ret)
{
    /* Mark op id as completed (independent of cb_ret) */
    hg_atomic_or32(&na_ucx_op_id->status, NA_UCX_OP_COMPLETED);

    /* Set callback ret */
    na_ucx_op_id->completion_data.callback_info.ret = cb_ret;

    /* Add OP to NA completion queue */
    na_cb_completion_add(na_ucx_op_id->context, &na_ucx_op_id->completion_data);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ucx_release(void *arg)
{
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) arg;

    NA_CHECK_SUBSYS_WARNING(op,
        na_ucx_op_id &&
            (!(hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_COMPLETED)),
        "Releasing resources from an uncompleted operation");

    if (na_ucx_op_id && na_ucx_op_id->addr != NULL) {
        na_ucx_addr_ref_decr(na_ucx_op_id->addr);
        na_ucx_op_id->addr = NULL;
    }
}

/********************/
/* Plugin callbacks */
/********************/

static na_return_t
na_ucx_get_protocol_info(
    const struct na_info *na_info, struct na_protocol_info **na_protocol_info_p)
{
    const char *protocol_name =
        (na_info != NULL) ? na_info->protocol_name : NULL;
    char tl_name[UCT_TL_NAME_MAX];
    struct na_protocol_info *na_protocol_info = NULL;
    uct_component_h *components = NULL;
    unsigned i, num_components;
    ucs_status_t status;
    na_return_t ret;

    /* parse protocol_name if provided */
    if ((protocol_name != NULL) && (strstr(protocol_name, "_") != NULL)) {
        ret =
            na_uct_get_transport_alias(protocol_name, tl_name, sizeof(tl_name));
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret,
            "Could not get protocol alias for %s", protocol_name);

        protocol_name = tl_name;
    }

    status = uct_query_components(&components, &num_components);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "uct_query_components() failed (%s)",
        ucs_status_string(status));

    for (i = 0; i < num_components; i++) {
        ret = na_uct_component_query(
            components[i], protocol_name, &na_protocol_info);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not query component");
    }

    uct_release_component_list(components);

    *na_protocol_info_p = na_protocol_info;

    return NA_SUCCESS;

error:
    if (components != NULL)
        uct_release_component_list(components);

    return ret;
}

/*---------------------------------------------------------------------------*/
static bool
na_ucx_check_protocol(const char *protocol_name)
{
    ucp_config_t *config = NULL;
    ucp_params_t params = {
        .field_mask = UCP_PARAM_FIELD_FEATURES, .features = NA_UCX_FEATURES};
    ucp_context_h context = NULL;
    ucs_status_t status;
    bool accept = false;

    status = ucp_config_read(NULL, NULL, &config);
    NA_CHECK_SUBSYS_ERROR_NORET(cls, status != UCS_OK, done,
        "ucp_config_read() failed (%s)", ucs_status_string(status));

    /* Try to use requested protocol */
    status = ucp_config_modify(config, "TLS", protocol_name);
    NA_CHECK_SUBSYS_ERROR_NORET(cls, status != UCS_OK, done,
        "ucp_config_modify() failed (%s)", ucs_status_string(status));

    status = ucp_init(&params, config, &context);
    if (status == UCS_OK) {
        accept = true;
        ucp_cleanup(context);
    }

done:
    if (config)
        ucp_config_release(config);

    return accept;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen)
{
    const struct na_init_info *na_init_info = &na_info->na_init_info;
    struct na_ucx_class *na_ucx_class = NULL;
#ifdef NA_UCX_HAS_LIB_QUERY
    ucp_lib_attr_t ucp_lib_attrs;
#endif
    char *net_device = NULL;
    struct sockaddr *src_sockaddr = NULL;
    socklen_t src_addrlen = 0;
    struct sockaddr_storage ucp_listener_ss_addr;
    ucs_sock_addr_t addr_key = {.addr = NULL, .addrlen = 0};
    ucp_config_t *config = NULL;
    bool no_wait = false;
    size_t unexpected_size_max = 0, expected_size_max = 0;
    ucs_thread_mode_t context_thread_mode, worker_thread_mode;
    na_return_t ret;
#ifdef NA_UCX_HAS_ADDR_POOL
    unsigned int i;
#endif
#ifdef NA_UCX_HAS_LIB_QUERY
    ucs_status_t status;
#endif
    bool multi_dev = false;

    /* Progress mode */
    if (na_init_info->progress_mode & NA_NO_BLOCK)
        no_wait = true;
    /* Max contexts */
    // if (na_init_info->max_contexts)
    //     context_max = na_init_info->max_contexts;
    /* Sizes */
    if (na_init_info->max_unexpected_size)
        unexpected_size_max = na_init_info->max_unexpected_size;
    if (na_init_info->max_expected_size)
        expected_size_max = na_init_info->max_expected_size;
    /* Thread mode */
    if (na_init_info->thread_mode & NA_THREAD_MODE_SINGLE) {
        context_thread_mode = UCS_THREAD_MODE_SINGLE;
        worker_thread_mode = UCS_THREAD_MODE_SINGLE;
    } else {
        context_thread_mode = UCS_THREAD_MODE_MULTI;
        worker_thread_mode = UCS_THREAD_MODE_MULTI;
    }

#ifdef NA_UCX_HAS_LIB_QUERY
    ucp_lib_attrs.field_mask = UCP_LIB_ATTR_FIELD_MAX_THREAD_LEVEL;
    status = ucp_lib_query(&ucp_lib_attrs);
    NA_CHECK_SUBSYS_ERROR(cls, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_context_query: %s",
        ucs_status_string(status));
    NA_CHECK_SUBSYS_ERROR(cls,
        (ucp_lib_attrs.field_mask & UCP_LIB_ATTR_FIELD_MAX_THREAD_LEVEL) == 0,
        error, ret, NA_PROTONOSUPPORT,
        "lib attributes contain no max thread level");

    /* Best effort to ensure thread safety
     * (no error to allow for UCS_THREAD_MODE_SERIALIZED) */
    if (worker_thread_mode != UCS_THREAD_MODE_SINGLE &&
        ucp_lib_attrs.max_thread_level == UCS_THREAD_MODE_SERIALIZED) {
        worker_thread_mode = UCS_THREAD_MODE_SERIALIZED;
        NA_LOG_SUBSYS_WARNING(cls, "Max worker thread level is: %s",
            ucs_thread_mode_names[worker_thread_mode]);
    }
#endif

    /* Parse hostname info and get device / listener IP */
    ret = na_ucx_parse_hostname_info(na_info->host_name,
        na_init_info->ip_subnet ? na_init_info->ip_subnet : NULL, listen,
        &net_device, &src_sockaddr, &src_addrlen);
    NA_CHECK_SUBSYS_NA_ERROR(
        cls, error, ret, "na_ucx_parse_hostname_info() failed");

    /* Multi-rail */
    if (net_device != NULL && strstr(net_device, ","))
        multi_dev = true;

    /* Create new UCX class */
    na_ucx_class = na_ucx_class_alloc();
    NA_CHECK_SUBSYS_ERROR(cls, na_ucx_class == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA UCX class");

    /* Keep a copy of the protocol name */
    na_ucx_class->protocol_name = (na_info->protocol_name)
                                      ? strdup(na_info->protocol_name)
                                      : strdup(NA_UCX_PROTOCOL_DEFAULT);
    NA_CHECK_SUBSYS_ERROR(cls, na_ucx_class->protocol_name == NULL, error, ret,
        NA_NOMEM, "Could not dup NA protocol name");

    /* Set wait mode */
    na_ucx_class->no_wait = no_wait;

    /* TODO may need to query UCX */
    na_ucx_class->unexpected_size_max =
        unexpected_size_max ? unexpected_size_max : NA_UCX_MSG_SIZE_MAX;
    na_ucx_class->expected_size_max =
        expected_size_max ? expected_size_max : NA_UCX_MSG_SIZE_MAX;

    /* Init config options */
    ret = na_ucp_config_init(na_info->protocol_name, net_device, &config);
    NA_CHECK_SUBSYS_NA_ERROR(
        cls, error, ret, "Could not initialize UCX config");

    /* Create UCP context and release config */
    ret = na_ucp_context_create(config, no_wait, context_thread_mode,
        &na_ucx_class->ucp_context, &na_ucx_class->ucp_request_size);
    na_ucp_config_release(config);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not create UCX context");

    /* No longer needed */
    free(net_device);
    net_device = NULL;

    /* Create single worker */
    ret = na_ucp_worker_create(na_ucx_class->ucp_context, worker_thread_mode,
        &na_ucx_class->ucp_worker);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not create UCX worker");

    /* Set AM handler for unexpected messages */
    ret = na_ucp_set_am_handler(
        na_ucx_class->ucp_worker, na_ucp_am_recv_cb, (void *) na_ucx_class);
    NA_CHECK_SUBSYS_NA_ERROR(
        cls, error, ret, "Could not set handler for receiving active messages");

    /* Create listener if we're listening */
    if (listen) {
        ret = na_ucp_listener_create(na_ucx_class->ucp_worker, src_sockaddr,
            src_addrlen, (void *) na_ucx_class, &na_ucx_class->ucp_listener,
            &ucp_listener_ss_addr);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not create UCX listener");

        addr_key = (ucs_sock_addr_t){
            .addr = (const struct sockaddr *) &ucp_listener_ss_addr,
            .addrlen = sizeof(ucp_listener_ss_addr)};
    } else if (!multi_dev)
        addr_key =
            (ucs_sock_addr_t){.addr = src_sockaddr, .addrlen = src_addrlen};

#ifdef NA_UCX_HAS_ADDR_POOL
    /* Create pool of addresses */
    for (i = 0; i < NA_UCX_ADDR_POOL_SIZE; i++) {
        struct na_ucx_addr *na_ucx_addr = na_ucx_addr_alloc(na_ucx_class);
        STAILQ_INSERT_TAIL(&na_ucx_class->addr_pool.queue, na_ucx_addr, entry);
    }
#endif

    /* Create self address */
    ret = na_ucx_addr_create(na_ucx_class, &addr_key, &na_ucx_class->self_addr);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not create self address");

    /* Attach worker address */
    ret = na_ucp_worker_get_address(na_ucx_class->ucp_worker,
        &na_ucx_class->self_addr->worker_addr,
        &na_ucx_class->self_addr->worker_addr_len);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not get worker address");

    /* Register initial mempool */
#ifdef NA_UCX_HAS_MEM_POOL
    na_ucx_class->mem_pool = hg_mem_pool_create(
        MAX(na_ucx_class->unexpected_size_max, na_ucx_class->expected_size_max),
        NA_UCX_MEM_CHUNK_COUNT, NA_UCX_MEM_BLOCK_COUNT, na_ucp_mem_buf_register,
        0, na_ucp_mem_buf_deregister, (void *) na_ucx_class);
    NA_CHECK_SUBSYS_ERROR(cls, na_ucx_class->mem_pool == NULL, error, ret,
        NA_NOMEM,
        "Could not create memory pool with %d blocks of size %d x %zu bytes",
        NA_UCX_MEM_BLOCK_COUNT, NA_UCX_MEM_CHUNK_COUNT,
        MAX(na_ucx_class->unexpected_size_max,
            na_ucx_class->expected_size_max));
#endif

    na_class->plugin_class = (void *) na_ucx_class;

    /* No longer needed */
    free(src_sockaddr);

    return NA_SUCCESS;

error:
    free(net_device);
    free(src_sockaddr);
    if (na_ucx_class)
        na_ucx_class_free(na_ucx_class);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_finalize(na_class_t *na_class)
{
    struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);
    hg_hash_table_iter_t addr_table_iter;
    na_return_t ret = NA_SUCCESS;

    if (na_ucx_class == NULL)
        return ret;

    NA_CHECK_SUBSYS_ERROR(cls, hg_atomic_get32(&na_ucx_class->ncontexts) != 0,
        done, ret, NA_BUSY, "Contexts were not destroyed (%d remaining)",
        hg_atomic_get32(&na_ucx_class->ncontexts));

    /* Iterate over remaining addresses and free them */
    hg_hash_table_iterate(na_ucx_class->addr_map.key_map, &addr_table_iter);
    while (hg_hash_table_iter_has_more(&addr_table_iter)) {
        struct na_ucx_addr *na_ucx_addr =
            (struct na_ucx_addr *) hg_hash_table_iter_next(&addr_table_iter);
        na_ucx_addr_destroy(na_ucx_addr);
    }

#ifdef NA_UCX_HAS_ADDR_POOL
    /* Free address pool */
    while (!STAILQ_EMPTY(&na_ucx_class->addr_pool.queue)) {
        struct na_ucx_addr *na_ucx_addr =
            STAILQ_FIRST(&na_ucx_class->addr_pool.queue);
        STAILQ_REMOVE_HEAD(&na_ucx_class->addr_pool.queue, entry);
        na_ucx_addr_destroy(na_ucx_addr);
    }
#endif

    na_ucx_class_free(na_ucx_class);
    na_class->plugin_class = NULL;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_op_id_t *
na_ucx_op_create(na_class_t *na_class, unsigned long NA_UNUSED flags)
{
    struct na_ucx_op_id *na_ucx_op_id = NULL;

    /* When using UCP requests, OP IDs must have enough space to fit the
     * UCP request data as a header */
    na_ucx_op_id = hg_mem_header_alloc(NA_UCX_CLASS(na_class)->ucp_request_size,
        alignof(struct na_ucx_op_id), sizeof(*na_ucx_op_id));
    NA_CHECK_SUBSYS_ERROR_NORET(op, na_ucx_op_id == NULL, out,
        "Could not allocate NA OFI operation ID");

    memset(na_ucx_op_id, 0, sizeof(struct na_ucx_op_id));

    /* Completed by default */
    hg_atomic_init32(&na_ucx_op_id->status, NA_UCX_OP_COMPLETED);

out:
    return (na_op_id_t *) na_ucx_op_id;
}

/*---------------------------------------------------------------------------*/
static void
na_ucx_op_destroy(na_class_t NA_UNUSED *na_class, na_op_id_t *op_id)
{
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) op_id;

    NA_CHECK_SUBSYS_WARNING(op,
        !(hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_COMPLETED),
        "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ucx_op_id->completion_data.callback_info.type));

    hg_mem_header_free(NA_UCX_CLASS(na_class)->ucp_request_size,
        alignof(struct na_ucx_op_id), na_ucx_op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr_p)
{
    char host_string[NI_MAXHOST];
    char serv_string[NI_MAXSERV];
    struct addrinfo hints, *hostname_res = NULL;
    struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);
    struct na_ucx_addr *na_ucx_addr = NULL;
    ucs_sock_addr_t addr_key = {.addr = NULL, .addrlen = 0};
    na_return_t ret;
    int rc;

    /* Only support 'all' or same protocol */
    NA_CHECK_SUBSYS_ERROR(fatal,
        strncmp(name, "all", strlen("all")) &&
            strncmp(name, na_ucx_class->protocol_name,
                strlen(na_ucx_class->protocol_name)),
        error, ret, NA_PROTONOSUPPORT,
        "Protocol not supported by this class (%s)",
        na_ucx_class->protocol_name);

    /* Retrieve address */
    rc = sscanf(name, "%*[^:]://%[^:]:%s", host_string, serv_string);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 2, error, ret, NA_PROTONOSUPPORT,
        "Malformed address string");

    NA_LOG_SUBSYS_DEBUG(addr, "Host %s, Serv %s", host_string, serv_string);

    /* Resolve address */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    hints.ai_protocol = 0;
    rc = getaddrinfo(host_string, serv_string, &hints, &hostname_res);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret, NA_PROTOCOL_ERROR,
        "getaddrinfo() failed (%s)", gai_strerror(rc));

    /* Lookup address from table */
    addr_key = (ucs_sock_addr_t){
        .addr = hostname_res->ai_addr, .addrlen = hostname_res->ai_addrlen};
    na_ucx_addr = na_ucx_addr_map_lookup(&na_ucx_class->addr_map, &addr_key);

    if (!na_ucx_addr) {
        na_return_t na_ret;

        NA_LOG_SUBSYS_DEBUG(
            addr, "Inserting new address (%s:%s)", host_string, serv_string);

        /* Insert new entry and create new address if needed */
        na_ret = na_ucx_addr_map_insert(na_ucx_class, &na_ucx_class->addr_map,
            &addr_key, NULL, &na_ucx_addr);
        freeaddrinfo(hostname_res);
        NA_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS && na_ret != NA_EXIST,
            error, ret, na_ret, "Could not insert new address");
    } else {
        freeaddrinfo(hostname_res);
        NA_LOG_SUBSYS_DEBUG(addr, "Address for %s was found", host_string);
    }

    na_ucx_addr_ref_incr(na_ucx_addr);

    *addr_p = (na_addr_t *) na_ucx_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ucx_addr_free(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    na_ucx_addr_ref_decr((struct na_ucx_addr *) addr);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ucx_addr_self(na_class_t *na_class, na_addr_t **addr_p)
{
    na_ucx_addr_ref_incr(NA_UCX_CLASS(na_class)->self_addr);
    *addr_p = (na_addr_t *) NA_UCX_CLASS(na_class)->self_addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ucx_addr_dup(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr, na_addr_t **new_addr_p)
{
    na_ucx_addr_ref_incr((struct na_ucx_addr *) addr);
    *new_addr_p = addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static bool
na_ucx_addr_cmp(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr1, na_addr_t *addr2)
{
    return addr1 == addr2;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_ucx_addr_is_self(na_class_t *na_class, na_addr_t *addr)
{
    return NA_UCX_CLASS(na_class)->self_addr == (struct na_ucx_addr *) addr;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size_p, na_addr_t *addr)
{
    struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);
    struct na_ucx_addr *na_ucx_addr = (struct na_ucx_addr *) addr;
    char host_string[NI_MAXHOST];
    char serv_string[NI_MAXSERV];
    size_t buf_size;
    na_return_t ret;
    int rc;

    NA_CHECK_SUBSYS_ERROR(addr, na_ucx_addr->addr_key.addrlen == 0, error, ret,
        NA_OPNOTSUPPORTED, "Cannot convert address to string");

    rc = getnameinfo(na_ucx_addr->addr_key.addr, na_ucx_addr->addr_key.addrlen,
        host_string, sizeof(host_string), serv_string, sizeof(serv_string),
        NI_NUMERICHOST | NI_NUMERICSERV);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret, NA_PROTOCOL_ERROR,
        "getnameinfo() failed (%s)", gai_strerror(rc));

    buf_size = strlen(host_string) + strlen(serv_string) +
               strlen(na_ucx_class->protocol_name) + 5;
    if (buf) {
        rc = snprintf(buf, buf_size, "%s://%s:%s", na_ucx_class->protocol_name,
            host_string, serv_string);
        NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > (int) buf_size, error, ret,
            NA_OVERFLOW, "snprintf() failed or name truncated, rc: %d", rc);

        NA_LOG_SUBSYS_DEBUG(addr, "Converted UCX address (%p) to string (%s)",
            (void *) na_ucx_addr, buf);
    }
    *buf_size_p = buf_size;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ucx_addr_get_serialize_size(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    return ((struct na_ucx_addr *) addr)->worker_addr_len + sizeof(uint64_t);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_serialize(
    na_class_t NA_UNUSED *na_class, void *buf, size_t buf_size, na_addr_t *addr)
{
    struct na_ucx_addr *na_ucx_addr = (struct na_ucx_addr *) addr;
    char *buf_ptr = (char *) buf;
    size_t buf_size_left = buf_size;
    uint64_t len;
    na_return_t ret = NA_SUCCESS;

    NA_CHECK_SUBSYS_ERROR(addr, na_ucx_addr->worker_addr == NULL, done, ret,
        NA_PROTONOSUPPORT,
        "Serialization of addresses can only be done if worker address is "
        "available");
    NA_CHECK_SUBSYS_ERROR(addr, na_ucx_addr->worker_addr_len > buf_size, done,
        ret, NA_OVERFLOW,
        "Space left to encode worker address is not sufficient");

    /* Encode worker_addr_len and worker_addr */
    len = (uint64_t) na_ucx_addr->worker_addr_len;
    NA_ENCODE(done, ret, buf_ptr, buf_size_left, &len, uint64_t);
    memcpy(buf_ptr, na_ucx_addr->worker_addr, na_ucx_addr->worker_addr_len);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_addr_deserialize(
    na_class_t *na_class, na_addr_t **addr_p, const void *buf, size_t buf_size)
{
    struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);
    struct na_ucx_addr *na_ucx_addr = NULL;
    const char *buf_ptr = (const char *) buf;
    size_t buf_size_left = buf_size;
    ucp_address_t *worker_addr = NULL;
    size_t worker_addr_len = 0;
    uint64_t len = 0;
    na_return_t ret;

    /* Encode worker_addr_len and worker_addr */
    NA_DECODE(error, ret, buf_ptr, buf_size_left, &len, uint64_t);
    worker_addr_len = (size_t) len;

    NA_CHECK_SUBSYS_ERROR(addr, buf_size_left < worker_addr_len, error, ret,
        NA_OVERFLOW, "Space left to decode worker address is not sufficient");

    worker_addr = (ucp_address_t *) malloc(worker_addr_len);
    NA_CHECK_SUBSYS_ERROR(addr, worker_addr == NULL, error, ret, NA_NOMEM,
        "Could not allocate worker_addr");
    memcpy(worker_addr, buf_ptr, worker_addr_len);

    /* Create new address */
    ret = na_ucx_addr_create(na_ucx_class, NULL, &na_ucx_addr);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret, "Could not create address");

    /* Attach worker address */
    na_ucx_addr->worker_addr = worker_addr;
    na_ucx_addr->worker_addr_len = worker_addr_len;
    na_ucx_addr->worker_addr_alloc = true;

    /* Create EP */
    ret = na_ucp_connect_worker(na_ucx_class->ucp_worker, worker_addr,
        na_ucp_ep_error_cb, na_ucx_addr, &na_ucx_addr->ucp_ep);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not connect to remote worker");

    hg_atomic_or32(&na_ucx_addr->status, NA_UCX_ADDR_RESOLVED);

    *addr_p = (na_addr_t *) na_ucx_addr;

    return NA_SUCCESS;

error:
    if (na_ucx_addr)
        na_ucx_addr_destroy(na_ucx_addr);
    else if (worker_addr)
        free(worker_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ucx_msg_get_max_unexpected_size(const na_class_t *na_class)
{
    return NA_UCX_CLASS(na_class)->unexpected_size_max;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ucx_msg_get_max_expected_size(const na_class_t *na_class)
{
    return NA_UCX_CLASS(na_class)->expected_size_max;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_tag_t
na_ucx_msg_get_max_tag(const na_class_t NA_UNUSED *na_class)
{
    return NA_UCX_MAX_TAG;
}

/*---------------------------------------------------------------------------*/
static void *
na_ucx_msg_buf_alloc(na_class_t *na_class, size_t size,
    unsigned long NA_UNUSED flags, void **plugin_data_p)
{
    void *mem_ptr;

#ifdef NA_UCX_HAS_MEM_POOL
    mem_ptr = hg_mem_pool_alloc(
        NA_UCX_CLASS(na_class)->mem_pool, size, plugin_data_p);
    NA_CHECK_SUBSYS_ERROR_NORET(
        mem, mem_ptr == NULL, done, "Could not allocate buffer from pool");
#else
    mem_ptr = na_ucp_mem_alloc(
        NA_UCX_CLASS(na_class)->ucp_context, size, (ucp_mem_h *) plugin_data_p);
    NA_CHECK_SUBSYS_ERROR_NORET(
        mem, mem_ptr == NULL, done, "Could not allocate memory");
#endif

done:
    return mem_ptr;
}

/*---------------------------------------------------------------------------*/
static void
na_ucx_msg_buf_free(na_class_t *na_class, void *buf, void *plugin_data)
{
#ifdef NA_UCX_HAS_MEM_POOL
    hg_mem_pool_free(NA_UCX_CLASS(na_class)->mem_pool, buf, plugin_data);
#else
    (void) na_ucp_mem_free(
        NA_UCX_CLASS(na_class)->ucp_context, (ucp_mem_h) plugin_data);
    (void) buf;
#endif
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_msg_send_unexpected(na_class_t NA_UNUSED *na_class,
    na_context_t *context, na_cb_t callback, void *arg, const void *buf,
    size_t buf_size, void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_ucx_addr *na_ucx_addr = (struct na_ucx_addr *) dest_addr;
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ucx_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ucx_op_id->completion_data.callback_info.type));

    /* Check addr to ensure the EP for that addr is still valid */
    if (!(hg_atomic_get32(&na_ucx_addr->status) & NA_UCX_ADDR_RESOLVED)) {
        struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);

        ret = na_ucx_addr_map_update(
            na_ucx_class, &na_ucx_class->addr_map, na_ucx_addr);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, error, ret, "Could not update NA UCX address");
    }
    NA_CHECK_SUBSYS_ERROR(msg, na_ucx_addr->ucp_ep == NULL, error, ret,
        NA_ADDRNOTAVAIL, "UCP endpoint is NULL for that address");

    NA_UCX_OP_RESET(na_ucx_op_id, context, NA_CB_SEND_UNEXPECTED, callback, arg,
        na_ucx_addr);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ucx_op_id->info.msg = (struct na_ucx_msg_info){
        .buf.const_ptr = buf, .buf_size = buf_size, .tag = (ucp_tag_t) tag};

    ret = na_ucp_am_send(na_ucx_addr->ucp_ep, buf, buf_size,
        &na_ucx_op_id->info.msg.tag, na_ucx_op_id);
    NA_CHECK_SUBSYS_NA_ERROR(msg, release, ret, "Could not post msg send");

    return NA_SUCCESS;

release:
    NA_UCX_OP_RELEASE(na_ucx_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_op_id_t *op_id)
{
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ucx_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ucx_op_id->completion_data.callback_info.type));

    NA_UCX_OP_RESET(
        na_ucx_op_id, context, NA_CB_RECV_UNEXPECTED, callback, arg, NULL);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ucx_op_id->info.msg = (struct na_ucx_msg_info){
        .buf.ptr = buf, .buf_size = buf_size, .tag = (ucp_tag_t) 0};

    na_ucp_am_recv(NA_UCX_CLASS(na_class), na_ucx_op_id);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_msg_send_expected(na_class_t NA_UNUSED *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_ucx_addr *na_ucx_addr = (struct na_ucx_addr *) dest_addr;
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ucx_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ucx_op_id->completion_data.callback_info.type));

    /* Check addr to ensure the EP for that addr is still valid */
    if (!(hg_atomic_get32(&na_ucx_addr->status) & NA_UCX_ADDR_RESOLVED)) {
        struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);

        ret = na_ucx_addr_map_update(
            na_ucx_class, &na_ucx_class->addr_map, na_ucx_addr);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, error, ret, "Could not update NA UCX address");
    }
    NA_CHECK_SUBSYS_ERROR(msg, na_ucx_addr->ucp_ep == NULL, error, ret,
        NA_ADDRNOTAVAIL, "UCP endpoint is NULL for that address");

    NA_UCX_OP_RESET(
        na_ucx_op_id, context, NA_CB_SEND_EXPECTED, callback, arg, na_ucx_addr);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ucx_op_id->info.msg = (struct na_ucx_msg_info){
        .buf.const_ptr = buf, .buf_size = buf_size, .tag = (ucp_tag_t) tag};

    ret = na_ucp_msg_send(
        na_ucx_addr->ucp_ep, buf, buf_size, (ucp_tag_t) tag, na_ucx_op_id);
    NA_CHECK_SUBSYS_NA_ERROR(msg, release, ret, "Could not post msg send");

    return NA_SUCCESS;

release:
    NA_UCX_OP_RELEASE(na_ucx_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *source_addr,
    uint8_t NA_UNUSED source_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_ucx_addr *na_ucx_addr = (struct na_ucx_addr *) source_addr;
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ucx_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ucx_op_id->completion_data.callback_info.type));

    NA_UCX_OP_RESET(
        na_ucx_op_id, context, NA_CB_RECV_EXPECTED, callback, arg, na_ucx_addr);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ucx_op_id->info.msg = (struct na_ucx_msg_info){
        .buf.ptr = buf, .buf_size = buf_size, .tag = (ucp_tag_t) tag};

    ret = na_ucp_msg_recv(NA_UCX_CLASS(na_class)->ucp_worker, buf, buf_size,
        (ucp_tag_t) tag, na_ucx_op_id);
    NA_CHECK_SUBSYS_NA_ERROR(
        msg, release, ret, "Could not post expected msg recv");

    return NA_SUCCESS;

release:
    NA_UCX_OP_RELEASE(na_ucx_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_mem_handle_create(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, unsigned long flags, na_mem_handle_t **mem_handle_p)
{
    struct na_ucx_mem_handle *na_ucx_mem_handle = NULL;
    na_return_t ret;

    /* Allocate memory handle */
    na_ucx_mem_handle = (struct na_ucx_mem_handle *) calloc(
        1, sizeof(struct na_ucx_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_ucx_mem_handle == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA UCX memory handle");

    na_ucx_mem_handle->desc.base = (uint64_t) buf;
    na_ucx_mem_handle->desc.flags = flags & 0xff;
    na_ucx_mem_handle->desc.len = (uint64_t) buf_size;
    hg_atomic_init32(&na_ucx_mem_handle->type, NA_UCX_MEM_HANDLE_LOCAL);
    hg_thread_mutex_init(&na_ucx_mem_handle->rkey_unpack_lock);

    *mem_handle_p = (na_mem_handle_t *) na_ucx_mem_handle;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ucx_mem_handle_free(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    struct na_ucx_mem_handle *na_ucx_mem_handle =
        (struct na_ucx_mem_handle *) mem_handle;

    switch (hg_atomic_get32(&na_ucx_mem_handle->type)) {
        case NA_UCX_MEM_HANDLE_LOCAL:
            /* nothing to do here */
            break;
        case NA_UCX_MEM_HANDLE_REMOTE_UNPACKED:
            ucp_rkey_destroy(na_ucx_mem_handle->ucp_mr.rkey);
            NA_FALLTHROUGH;
        case NA_UCX_MEM_HANDLE_REMOTE_PACKED:
            free(na_ucx_mem_handle->rkey_buf);
            break;
        default:
            NA_LOG_SUBSYS_ERROR(mem, "Invalid memory handle type");
            break;
    }

    hg_thread_mutex_destroy(&na_ucx_mem_handle->rkey_unpack_lock);
    free(na_ucx_mem_handle);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ucx_mem_handle_get_max_segments(const na_class_t NA_UNUSED *na_class)
{
    return 1;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_mem_register(na_class_t *na_class, na_mem_handle_t *mem_handle,
    enum na_mem_type mem_type, uint64_t NA_UNUSED device)
{
    struct na_ucx_mem_handle *na_ucx_mem_handle =
        (struct na_ucx_mem_handle *) mem_handle;
    ucp_mem_map_params_t mem_map_params = {
        .field_mask =
            UCP_MEM_MAP_PARAM_FIELD_ADDRESS | UCP_MEM_MAP_PARAM_FIELD_LENGTH |
            UCP_MEM_MAP_PARAM_FIELD_PROT | UCP_MEM_MAP_PARAM_FIELD_MEMORY_TYPE,
        .address = (void *) na_ucx_mem_handle->desc.base,
        .length = (size_t) na_ucx_mem_handle->desc.len};
    size_t rkey_buf_size;
    ucs_status_t status;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(mem,
        hg_atomic_get32(&na_ucx_mem_handle->type) != NA_UCX_MEM_HANDLE_LOCAL,
        error, ret, NA_OPNOTSUPPORTED,
        "cannot register memory on remote handle");

    /* Set access mode */
    switch (na_ucx_mem_handle->desc.flags) {
        case NA_MEM_READ_ONLY:
            mem_map_params.prot =
                UCP_MEM_MAP_PROT_REMOTE_READ | UCP_MEM_MAP_PROT_LOCAL_READ;
            break;
        case NA_MEM_WRITE_ONLY:
            mem_map_params.prot =
                UCP_MEM_MAP_PROT_REMOTE_WRITE | UCP_MEM_MAP_PROT_LOCAL_WRITE;
            break;
        case NA_MEM_READWRITE:
            mem_map_params.prot =
                UCP_MEM_MAP_PROT_LOCAL_READ | UCP_MEM_MAP_PROT_LOCAL_WRITE |
                UCP_MEM_MAP_PROT_REMOTE_READ | UCP_MEM_MAP_PROT_REMOTE_WRITE;
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(
                mem, error, ret, NA_INVALID_ARG, "Invalid memory access flag");
            break;
    }

    /* Set memory type */
    switch (mem_type) {
        case NA_MEM_TYPE_CUDA:
            mem_map_params.memory_type = UCS_MEMORY_TYPE_CUDA;
            break;
        case NA_MEM_TYPE_ROCM:
            mem_map_params.memory_type = UCS_MEMORY_TYPE_ROCM;
            break;
        case NA_MEM_TYPE_ZE:
            NA_GOTO_SUBSYS_ERROR(
                mem, error, ret, NA_OPNOTSUPPORTED, "Unsupported memory type");
            break;
        case NA_MEM_TYPE_HOST:
            mem_map_params.memory_type = UCS_MEMORY_TYPE_HOST;
            break;
        case NA_MEM_TYPE_UNKNOWN:
        default:
            mem_map_params.memory_type = UCS_MEMORY_TYPE_UNKNOWN;
            break;
    }

    /* Register memory */
    status = ucp_mem_map(NA_UCX_CLASS(na_class)->ucp_context, &mem_map_params,
        &na_ucx_mem_handle->ucp_mr.mem);
    NA_CHECK_SUBSYS_ERROR(mem, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_mem_map() failed (%s)",
        ucs_status_string(status));

    /* Keep a copy of the rkey to share with the remote */
    /* TODO that could have been a good candidate for publish */
    status = ucp_rkey_pack(NA_UCX_CLASS(na_class)->ucp_context,
        na_ucx_mem_handle->ucp_mr.mem, &na_ucx_mem_handle->rkey_buf,
        &rkey_buf_size);
    NA_CHECK_SUBSYS_ERROR(mem, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_rkey_pack() failed (%s)",
        ucs_status_string(status));
    na_ucx_mem_handle->desc.rkey_buf_size = (uint64_t) rkey_buf_size;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_mem_deregister(na_class_t *na_class, na_mem_handle_t *mem_handle)
{
    struct na_ucx_mem_handle *na_ucx_mem_handle =
        (struct na_ucx_mem_handle *) mem_handle;
    ucs_status_t status;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(mem,
        hg_atomic_get32(&na_ucx_mem_handle->type) != NA_UCX_MEM_HANDLE_LOCAL,
        error, ret, NA_OPNOTSUPPORTED,
        "cannot unregister memory on remote handle");

    /* Deregister memory */
    status = ucp_mem_unmap(
        NA_UCX_CLASS(na_class)->ucp_context, na_ucx_mem_handle->ucp_mr.mem);
    NA_CHECK_SUBSYS_ERROR(mem, status != UCS_OK, error, ret,
        na_ucs_status_to_na(status), "ucp_mem_unmap() failed (%s)",
        ucs_status_string(status));
    na_ucx_mem_handle->ucp_mr.mem = NULL;

    /* TODO that could have been a good candidate for unpublish */
    ucp_rkey_buffer_release(na_ucx_mem_handle->rkey_buf);
    na_ucx_mem_handle->rkey_buf = NULL;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ucx_mem_handle_get_serialize_size(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    struct na_ucx_mem_handle *na_ucx_mem_handle =
        (struct na_ucx_mem_handle *) mem_handle;

    return sizeof(na_ucx_mem_handle->desc) +
           na_ucx_mem_handle->desc.rkey_buf_size;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_mem_handle_serialize(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, na_mem_handle_t *mem_handle)
{
    struct na_ucx_mem_handle *na_ucx_mem_handle =
        (struct na_ucx_mem_handle *) mem_handle;
    char *buf_ptr = (char *) buf;
    size_t buf_size_left = buf_size;
    na_return_t ret;

    /* Descriptor info */
    NA_ENCODE(error, ret, buf_ptr, buf_size_left, &na_ucx_mem_handle->desc,
        struct na_ucx_mem_desc);

    /* Encode rkey */
    memcpy(buf_ptr, na_ucx_mem_handle->rkey_buf,
        na_ucx_mem_handle->desc.rkey_buf_size);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_mem_handle_deserialize(na_class_t NA_UNUSED *na_class,
    na_mem_handle_t **mem_handle_p, const void *buf, size_t buf_size)
{
    struct na_ucx_mem_handle *na_ucx_mem_handle = NULL;
    const char *buf_ptr = (const char *) buf;
    size_t buf_size_left = buf_size;
    na_return_t ret;

    na_ucx_mem_handle =
        (struct na_ucx_mem_handle *) malloc(sizeof(struct na_ucx_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_ucx_mem_handle == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA UCX memory handle");
    na_ucx_mem_handle->rkey_buf = NULL;
    na_ucx_mem_handle->ucp_mr.rkey = NULL;
    hg_atomic_init32(&na_ucx_mem_handle->type, NA_UCX_MEM_HANDLE_REMOTE_PACKED);
    hg_thread_mutex_init(&na_ucx_mem_handle->rkey_unpack_lock);

    /* Descriptor info */
    NA_DECODE(error, ret, buf_ptr, buf_size_left, &na_ucx_mem_handle->desc,
        struct na_ucx_mem_desc);

    /* Packed rkey */
    na_ucx_mem_handle->rkey_buf = malloc(na_ucx_mem_handle->desc.rkey_buf_size);
    NA_CHECK_SUBSYS_ERROR(mem, na_ucx_mem_handle->rkey_buf == NULL, error, ret,
        NA_NOMEM, "Could not allocate rkey buffer");

    NA_CHECK_SUBSYS_ERROR(mem,
        buf_size_left < na_ucx_mem_handle->desc.rkey_buf_size, error, ret,
        NA_OVERFLOW, "Insufficient size left to copy rkey buffer");
    memcpy(na_ucx_mem_handle->rkey_buf, buf_ptr,
        na_ucx_mem_handle->desc.rkey_buf_size);

    *mem_handle_p = (na_mem_handle_t *) na_ucx_mem_handle;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    return na_ucx_rma(NA_UCX_CLASS(na_class), context, NA_CB_PUT, callback, arg,
        (struct na_ucx_mem_handle *) local_mem_handle, local_offset,
        (struct na_ucx_mem_handle *) remote_mem_handle, remote_offset, length,
        (struct na_ucx_addr *) remote_addr, (struct na_ucx_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    return na_ucx_rma(NA_UCX_CLASS(na_class), context, NA_CB_GET, callback, arg,
        (struct na_ucx_mem_handle *) local_mem_handle, local_offset,
        (struct na_ucx_mem_handle *) remote_mem_handle, remote_offset, length,
        (struct na_ucx_addr *) remote_addr, (struct na_ucx_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static int
na_ucx_poll_get_fd(na_class_t *na_class, na_context_t NA_UNUSED *context)
{
    struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);
    ucs_status_t status;
    int fd;

    if (na_ucx_class->no_wait)
        return -1;

    status = ucp_worker_get_efd(na_ucx_class->ucp_worker, &fd);
    NA_CHECK_SUBSYS_ERROR(poll, status != UCS_OK, error, fd, -1,
        "ucp_worker_get_efd() failed (%s)", ucs_status_string(status));

    return fd;

error:
    return -1;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_ucx_poll_try_wait(na_class_t *na_class, na_context_t NA_UNUSED *context)
{
    struct na_ucx_class *na_ucx_class = NA_UCX_CLASS(na_class);
    ucs_status_t status;

    if (na_ucx_class->no_wait)
        return false;

    status = ucp_worker_arm(na_ucx_class->ucp_worker);
    if (status == UCS_ERR_BUSY) {
        /* Events have already arrived */
        return false;
    } else if (status != UCS_OK) {
        NA_LOG_SUBSYS_ERROR(
            poll, "ucp_worker_arm() failed (%s)", ucs_status_string(status));
        return false;
    }

    return true;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ucx_poll(na_class_t *na_class, na_context_t NA_UNUSED *context,
    unsigned int *count_p)
{
    unsigned int count =
        ucp_worker_progress(NA_UCX_CLASS(na_class)->ucp_worker);
    if (count_p != NULL)
        *count_p = count;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ucx_cancel(
    na_class_t *na_class, na_context_t NA_UNUSED *context, na_op_id_t *op_id)
{
    struct na_ucx_op_id *na_ucx_op_id = (struct na_ucx_op_id *) op_id;
    na_cb_type_t cb_type;
    int32_t status;

    /* Exit if op has already completed */
    status = hg_atomic_get32(&na_ucx_op_id->status);
    if ((status & NA_UCX_OP_COMPLETED) || (status & NA_UCX_OP_ERRORED) ||
        (status & NA_UCX_OP_CANCELED) || (status & NA_UCX_OP_CANCELING))
        return NA_SUCCESS;

    cb_type = na_ucx_op_id->completion_data.callback_info.type;
    NA_LOG_SUBSYS_DEBUG(op, "Canceling operation ID %p (%s)",
        (void *) na_ucx_op_id, na_cb_type_to_string(cb_type));

    /* Must set canceling before we check for the retry queue */
    hg_atomic_or32(&na_ucx_op_id->status, NA_UCX_OP_CANCELING);

    /* Check if op_id is in unexpected op queue */
    if ((cb_type == NA_CB_RECV_UNEXPECTED) &&
        (hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_QUEUED)) {
        struct na_ucx_op_queue *op_queue =
            &NA_UCX_CLASS(na_class)->unexpected_op_queue;
        bool canceled = false;

        /* If dequeued by process_retries() in the meantime, we'll just let it
         * cancel there */

        hg_thread_spin_lock(&op_queue->lock);
        if (hg_atomic_get32(&na_ucx_op_id->status) & NA_UCX_OP_QUEUED) {
            TAILQ_REMOVE(&op_queue->queue, na_ucx_op_id, entry);
            hg_atomic_and32(&na_ucx_op_id->status, ~NA_UCX_OP_QUEUED);
            hg_atomic_or32(&na_ucx_op_id->status, NA_UCX_OP_CANCELED);
            canceled = true;
        }
        hg_thread_spin_unlock(&op_queue->lock);

        if (canceled)
            na_ucx_complete(na_ucx_op_id, NA_CANCELED);
    } else {
        /* Do best effort to cancel the operation */
        hg_atomic_or32(&na_ucx_op_id->status, NA_UCX_OP_CANCELED);
        ucp_request_cancel(
            NA_UCX_CLASS(na_class)->ucp_worker, (void *) na_ucx_op_id);
    }

    return NA_SUCCESS;
}
