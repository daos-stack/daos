/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#    define _GNU_SOURCE
#endif
#include "na_sm.h"
#include "na_plugin.h"

#include "mercury_atomic_queue.h"
#include "mercury_event.h"
#include "mercury_hash_table.h"
#include "mercury_mem.h"
#include "mercury_poll.h"
#include "mercury_thread_mutex.h"
#include "mercury_thread_rwlock.h"
#include "mercury_thread_spin.h"
#include "mercury_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NA_SM_HAS_UUID
#    include <uuid/uuid.h>
#endif

#ifdef _WIN32
#    include <process.h>
#else
#    include <fcntl.h>
#    include <ftw.h>
#    include <pwd.h>
#    include <sys/mman.h>
#    include <sys/resource.h>
#    include <sys/socket.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <sys/un.h>
#    include <unistd.h>
#    if defined(NA_SM_HAS_CMA)
#        include <limits.h>
#        include <sys/uio.h>
#    elif defined(__APPLE__)
#        include <mach/mach.h>
#        include <mach/mach_vm.h>
#    endif
#endif

/****************/
/* Local Macros */
/****************/

/* Default cache line size */
#define NA_SM_CACHE_LINE_SIZE HG_MEM_CACHE_LINE_SIZE

/* Default page size */
#define NA_SM_PAGE_SIZE HG_MEM_PAGE_SIZE

/* Default filenames/paths */
#define NA_SM_SHM_PATH  "/dev/shm"
#define NA_SM_SOCK_NAME "/sock"

/* Max filename length used for shared files */
#define NA_SM_MAX_FILENAME 64

/* Max number of shared-memory buffers (reserved by 64-bit atomic integer) */
#define NA_SM_NUM_BUFS 64

/* Size of shared-memory buffer */
#define NA_SM_COPY_BUF_SIZE NA_SM_PAGE_SIZE

/* Max number of fds used for cleanup */
#define NA_SM_CLEANUP_NFDS 16

/* Max number of peers */
#define NA_SM_MAX_PEERS (NA_CONTEXT_ID_MAX + 1)

/* Addr status bits */
#define NA_SM_ADDR_RESERVED   (1 << 0)
#define NA_SM_ADDR_CMD_PUSHED (1 << 1)
#define NA_SM_ADDR_RESOLVED   (1 << 2)

/* Msg sizes */
#define NA_SM_UNEXPECTED_SIZE NA_SM_COPY_BUF_SIZE
#define NA_SM_EXPECTED_SIZE   NA_SM_UNEXPECTED_SIZE

/* Max tag */
#define NA_SM_MAX_TAG NA_TAG_MAX

/* Maximum number of pre-allocated IOV entries */
#define NA_SM_IOV_STATIC_MAX (8)

/* Max events */
#define NA_SM_MAX_EVENTS 16

/* Op ID status bits */
#define NA_SM_OP_COMPLETED (1 << 0)
#define NA_SM_OP_RETRYING  (1 << 1)
#define NA_SM_OP_CANCELED  (1 << 2)
#define NA_SM_OP_QUEUED    (1 << 3)
#define NA_SM_OP_ERRORED   (1 << 4)

/* Private data access */
#define NA_SM_CLASS(na_class) ((struct na_sm_class *) (na_class->plugin_class))
#define NA_SM_CONTEXT(context)                                                 \
    ((struct na_sm_context *) (context->plugin_context))

/* Reset op ID */
#define NA_SM_OP_RESET(__op, __context, __cb_type, __cb, __arg, __addr)        \
    do {                                                                       \
        __op->context = __context;                                             \
        __op->completion_data.callback_info.type = __cb_type;                  \
        __op->completion_data.callback = __cb;                                 \
        __op->completion_data.callback_info.arg = __arg;                       \
        __op->addr = __addr;                                                   \
        na_sm_addr_ref_incr(__addr);                                           \
        hg_atomic_set32(&__op->status, 0);                                     \
    } while (0)

#define NA_SM_OP_RESET_UNEXPECTED_RECV(__op, __context, __cb, __arg)           \
    do {                                                                       \
        __op->context = __context;                                             \
        __op->completion_data.callback_info.type = NA_CB_RECV_UNEXPECTED;      \
        __op->completion_data.callback = __cb;                                 \
        __op->completion_data.callback_info.arg = __arg;                       \
        __op->completion_data.callback_info.info.recv_unexpected =             \
            (struct na_cb_info_recv_unexpected){                               \
                .actual_buf_size = 0, .source = NULL, .tag = 0};               \
        __op->addr = NULL;                                                     \
        hg_atomic_set32(&__op->status, 0);                                     \
    } while (0)

#define NA_SM_OP_RELEASE(__op)                                                 \
    do {                                                                       \
        if (__op->addr)                                                        \
            na_sm_addr_ref_decr(__op->addr);                                   \
        hg_atomic_set32(&__op->status, NA_SM_OP_COMPLETED);                    \
    } while (0)

/* Retrive endpoint key name */
#define NA_SM_SCAN_URI(str, addr_key_p)                                        \
    sscanf(str, "%d-%" SCNu8, &(addr_key_p)->pid, &(addr_key_p)->id)

/* Generate endpoint key name */
#define NA_SM_PRINT_URI(str, size, addr_key)                                   \
    snprintf(str, size, "%d-%" PRIu8, addr_key.pid, addr_key.id)

/* Generate SHM file name */
#define NA_SM_PRINT_SHM_NAME(str, size, uri)                                   \
    snprintf(str, size, NA_SM_SHM_PREFIX "-%s", uri)

/* Generate socket path */
#define NA_SM_PRINT_SOCK_PATH(str, size, uri)                                  \
    snprintf(str, size, NA_SM_TMP_DIRECTORY "/" NA_SM_SHM_PREFIX "-%s", uri);

#ifndef HG_UTIL_HAS_SYSEVENTFD_H
#    define NA_SM_PRINT_FIFO_NAME(str, size, uri, index, pair)                 \
        snprintf(str, size,                                                    \
            NA_SM_TMP_DIRECTORY "/" NA_SM_SHM_PREFIX "-%s/fifo-%u-%c", uri,    \
            index, pair)
#endif

/* Get IOV */
#define NA_SM_IOV(x)                                                           \
    ((x)->info.iovcnt > NA_SM_IOV_STATIC_MAX) ? (x)->iov.d : (x)->iov.s

/************************************/
/* Local Type and Struct Definition */
/************************************/

/* Msg header */
NA_PACKED(union na_sm_msg_hdr {
    struct {
        unsigned int tag : 32;      /* Message tag : UINT MAX */
        unsigned int buf_size : 16; /* Buffer length: 4KB MAX */
        unsigned int buf_idx : 8;   /* Index reserved: 64 MAX */
        unsigned int type : 8;      /* Message type */
    } hdr;
    uint64_t val;
});

/* Make sure this is cache-line aligned */
union na_sm_cacheline_atomic_int64 {
    hg_atomic_int64_t val;
    char pad[NA_SM_CACHE_LINE_SIZE];
};

union na_sm_cacheline_atomic_int256 {
    hg_atomic_int64_t val[4];
    char pad[NA_SM_CACHE_LINE_SIZE];
};

/* Msg buffers (page aligned) */
struct na_sm_copy_buf {
    hg_thread_spin_t buf_locks[NA_SM_NUM_BUFS];    /* Locks on buffers */
    char buf[NA_SM_NUM_BUFS][NA_SM_COPY_BUF_SIZE]; /* Array of buffers */
    union na_sm_cacheline_atomic_int64 available;  /* Available bitmask */
};

/* Msg queue (allocate queue's flexible array member statically) */
struct na_sm_msg_queue {
    hg_atomic_int32_t prod_head;
    hg_atomic_int32_t prod_tail;
    unsigned int prod_size;
    unsigned int prod_mask;
    uint64_t drops;
    NA_ALIGNED(hg_atomic_int32_t cons_head, HG_MEM_CACHE_LINE_SIZE);
    hg_atomic_int32_t cons_tail;
    unsigned int cons_size;
    unsigned int cons_mask;
    NA_ALIGNED(hg_atomic_int64_t ring[NA_SM_NUM_BUFS], HG_MEM_CACHE_LINE_SIZE);
};

/* Shared queue pair */
struct na_sm_queue_pair {
    struct na_sm_msg_queue tx_queue; /* Send queue */
    struct na_sm_msg_queue rx_queue; /* Recv queue */
};

/* Cmd values */
enum na_sm_cmd { NA_SM_RESERVED = 1, NA_SM_RELEASED };

/* Cmd header */
NA_PACKED(union na_sm_cmd_hdr {
    struct {
        unsigned int pid : 32;     /* PID */
        unsigned int id : 8;       /* ID */
        unsigned int pair_idx : 8; /* Index reserved */
        unsigned int type : 8;     /* Cmd type */
        unsigned int pad : 8;      /* 8 bits left */
    } hdr;
    uint64_t val;
});

/* Cmd queue (allocate queue's flexible array member statically) */
struct na_sm_cmd_queue {
    hg_atomic_int32_t prod_head;
    hg_atomic_int32_t prod_tail;
    unsigned int prod_size;
    unsigned int prod_mask;
    uint64_t drops;
    NA_ALIGNED(hg_atomic_int32_t cons_head, HG_MEM_CACHE_LINE_SIZE);
    hg_atomic_int32_t cons_tail;
    unsigned int cons_size;
    unsigned int cons_mask;
    /* To be safe, make the queue twice as large */
    NA_ALIGNED(
        hg_atomic_int64_t ring[NA_SM_MAX_PEERS * 2], HG_MEM_CACHE_LINE_SIZE);
};

/* Address key */
struct na_sm_addr_key {
    pid_t pid;  /* PID */
    uint8_t id; /* SM ID */
};

/* Shared region */
struct na_sm_region {
    struct na_sm_addr_key addr_key;  /* Region IDs */
    struct na_sm_copy_buf copy_bufs; /* Pool of msg buffers */
    NA_ALIGNED(struct na_sm_queue_pair queue_pairs[NA_SM_MAX_PEERS],
        NA_SM_PAGE_SIZE);                          /* Msg queue pairs */
    struct na_sm_cmd_queue cmd_queue;              /* Cmd queue */
    union na_sm_cacheline_atomic_int256 available; /* Available pairs */
};

/* Poll type */
enum na_sm_poll_type {
    NA_SM_POLL_SOCK = 1,
    NA_SM_POLL_RX_NOTIFY,
    NA_SM_POLL_TX_NOTIFY
};

/* Address */
struct na_sm_addr {
    hg_thread_mutex_t resolve_lock;     /* Lock to resolve address */
    LIST_ENTRY(na_sm_addr) entry;       /* Entry in poll list */
    struct na_sm_addr_key addr_key;     /* Address key */
    struct na_sm_endpoint *endpoint;    /* Endpoint */
    struct na_sm_region *shared_region; /* Shared-memory region */
    struct na_sm_msg_queue *tx_queue;   /* Pointer to shared tx queue */
    struct na_sm_msg_queue *rx_queue;   /* Pointer to shared rx queue */
    char *uri;                          /* Generated URI */
    int tx_notify;                      /* Notify fd for tx queue */
    int rx_notify;                      /* Notify fd for rx queue */
    enum na_sm_poll_type tx_poll_type;  /* Tx poll type */
    enum na_sm_poll_type rx_poll_type;  /* Rx poll type */
    hg_atomic_int32_t refcount;         /* Ref count */
    hg_atomic_int32_t status;           /* Status bits */
    uint8_t queue_pair_idx;             /* Shared queue pair index */
    bool unexpected;                    /* Unexpected address */
};

/* Address list */
struct na_sm_addr_list {
    LIST_HEAD(, na_sm_addr) list;
    hg_thread_spin_t lock;
};

/* Map (used to cache addresses) */
struct na_sm_map {
    hg_thread_rwlock_t lock;
    hg_hash_table_t *map;
};

/* Memory descriptor info */
struct na_sm_mem_desc_info {
    unsigned long iovcnt; /* Segment count */
    size_t len;           /* Size of region */
    uint8_t flags;        /* Flag of operation access */
};

/* IOV descriptor */
union na_sm_iov {
    struct iovec s[NA_SM_IOV_STATIC_MAX]; /* Single segment */
    struct iovec *d;                      /* Multiple segments */
};

/* Memory handle */
struct na_sm_mem_handle {
    struct na_sm_mem_desc_info info; /* Segment info */
    union na_sm_iov iov;             /* Remain last */
};

/* Msg info */
struct na_sm_msg_info {
    union {
        const void *const_ptr;
        void *ptr;
    } buf;
    size_t buf_size;
    na_tag_t tag;
};

/* Unexpected msg info */
struct na_sm_unexpected_info {
    STAILQ_ENTRY(na_sm_unexpected_info) entry;
    struct na_sm_addr *na_sm_addr;
    void *buf;
    size_t buf_size;
    na_tag_t tag;
};

/* Unexpected msg queue */
struct na_sm_unexpected_msg_queue {
    STAILQ_HEAD(, na_sm_unexpected_info) queue;
    hg_thread_spin_t lock;
};

/* RMA op */
typedef na_return_t (*na_sm_process_vm_op_t)(pid_t pid,
    const struct iovec *local_iov, unsigned long liovcnt,
    const struct iovec *remote_iov, unsigned long riovcnt, size_t length);

/* Operation ID */
struct na_sm_op_id {
    struct na_cb_completion_data completion_data; /* Completion data */
    union {
        struct na_sm_msg_info msg;
    } info;                         /* Op info                  */
    TAILQ_ENTRY(na_sm_op_id) entry; /* Entry in queue           */
    na_class_t *na_class;           /* NA class associated      */
    na_context_t *context;          /* NA context associated    */
    struct na_sm_addr *addr;        /* Address associated       */
    hg_atomic_int32_t status;       /* Operation status         */
};

/* Op ID queue */
struct na_sm_op_queue {
    TAILQ_HEAD(, na_sm_op_id) queue;
    hg_thread_spin_t lock;
};

/* Endpoint */
struct na_sm_endpoint {
    struct na_sm_map addr_map; /* Address map */
    struct na_sm_unexpected_msg_queue
        unexpected_msg_queue;                  /* Unexpected msg queue */
    struct na_sm_op_queue unexpected_op_queue; /* Unexpected op queue */
    struct na_sm_op_queue expected_op_queue;   /* Expected op queue */
    struct na_sm_op_queue retry_op_queue;      /* Retry op queue */
    struct na_sm_addr_list poll_addr_list;     /* List of addresses to poll */
    struct na_sm_addr *source_addr;            /* Source addr */
    hg_poll_set_t *poll_set;                   /* Poll set */
    int sock;                                  /* Sock fd */
    enum na_sm_poll_type sock_poll_type;       /* Sock poll type */
    hg_atomic_int32_t nofile;                  /* Number of opened fds */
    uint32_t nofile_max;                       /* Max number of fds */
    bool listen;                               /* Listen on sock */
};

/* Private context */
struct na_sm_context {
    struct hg_poll_event events[NA_SM_MAX_EVENTS];
};

/* Private data */
struct na_sm_class {
    struct na_sm_endpoint endpoint; /* Endpoint */
    size_t iov_max;                 /* Max number of IOVs */
    uint8_t context_max;            /* Max number of contexts */
};

/********************/
/* Local Prototypes */
/********************/

#ifdef NA_SM_HAS_CMA
/**
 * Get value from ptrace_scope.
 */
static int
na_sm_get_ptrace_scope_value(void);
#endif

/**
 * Convert errno to NA return values.
 */
static na_return_t
na_sm_errno_to_na(int rc);

/**
 * Map shared-memory object.
 */
static void *
na_sm_shm_map(const char *name, size_t length, bool create);

/**
 * Unmap shared-memory object.
 */
static na_return_t
na_sm_shm_unmap(const char *name, void *addr, size_t length);

/**
 * Clean up dangling shm segments.
 */
static int
na_sm_shm_cleanup(
    const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

/**
 * Initialize queue.
 */
static void
na_sm_msg_queue_init(struct na_sm_msg_queue *na_sm_queue);

/**
 * Multi-producer enqueue.
 */
static NA_INLINE bool
na_sm_msg_queue_push(
    struct na_sm_msg_queue *na_sm_queue, const union na_sm_msg_hdr *msg_hdr);

/**
 * Multi-consumer dequeue.
 */
static NA_INLINE bool
na_sm_msg_queue_pop(
    struct na_sm_msg_queue *na_sm_msg_queue, union na_sm_msg_hdr *msg_hdr);

/**
 * Check whether queue is empty.
 */
static NA_INLINE bool
na_sm_msg_queue_is_empty(struct na_sm_msg_queue *na_sm_queue);

/**
 * Initialize queue.
 */
static void
na_sm_cmd_queue_init(struct na_sm_cmd_queue *na_sm_queue);

/**
 * Multi-producer enqueue.
 */
static NA_INLINE bool
na_sm_cmd_queue_push(
    struct na_sm_cmd_queue *na_sm_queue, const union na_sm_cmd_hdr *cmd_hdr);

/**
 * Multi-consumer dequeue.
 */
static NA_INLINE bool
na_sm_cmd_queue_pop(
    struct na_sm_cmd_queue *na_sm_queue, union na_sm_cmd_hdr *cmd_hdr);

/**
 * Key hash for hash table.
 */
static NA_INLINE unsigned int
na_sm_addr_key_hash(hg_hash_table_key_t key);

/**
 * Compare key.
 */
static NA_INLINE int
na_sm_addr_key_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Get SM address from string.
 */
static na_return_t
na_sm_string_to_addr(
    const char *str, char *uri, size_t size, struct na_sm_addr_key *addr_key_p);

/**
 * Open shared-memory region.
 */
static na_return_t
na_sm_region_open(const char *uri, bool create, struct na_sm_region **region_p);

/**
 * Close shared-memory region.
 */
static na_return_t
na_sm_region_close(const char *uri, struct na_sm_region *region);

/**
 * Retrieve address key from region.
 */
static na_return_t
na_sm_region_get_addr_key(const char *uri, struct na_sm_addr_key *addr_key_p);

/**
 * Open UNIX domain socket.
 */
static na_return_t
na_sm_sock_open(const char *uri, bool create, int *sock);

/**
 * Close socket.
 */
static na_return_t
na_sm_sock_close(const char *uri, int sock);

/**
 * Create tmp path for UNIX socket.
 */
static na_return_t
na_sm_sock_path_create(const char *pathname);

/**
 * Remove tmp path for UNIX socket.
 */
static na_return_t
na_sm_sock_path_remove(const char *pathname);

/**
 * Clean up tmp paths for UNIX socket.
 */
static int
na_sm_sock_path_cleanup(
    const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

/**
 * Create event.
 */
static na_return_t
na_sm_event_create(
    const char *uri, uint8_t pair_index, unsigned char pair, int *event);

/**
 * Destroy event.
 */
static na_return_t
na_sm_event_destroy(const char *uri, uint8_t pair_index, unsigned char pair,
    bool remove, int event);

/**
 * Set event.
 */
static NA_INLINE na_return_t
na_sm_event_set(int event);

/**
 * Get event.
 */
static NA_INLINE na_return_t
na_sm_event_get(int event, bool *signaled);

/**
 * Register addr to poll set.
 */
static na_return_t
na_sm_poll_register(hg_poll_set_t *poll_set, int fd, void *ptr);

/**
 * Deregister addr from poll set.
 */
static na_return_t
na_sm_poll_deregister(hg_poll_set_t *poll_set, int fd);

/**
 * Open shared-memory endpoint.
 */
static na_return_t
na_sm_endpoint_open(struct na_sm_endpoint *na_sm_endpoint, const char *name,
    bool listen, bool no_wait, uint32_t nofile_max);

/**
 * Close shared-memory endpoint.
 */
static na_return_t
na_sm_endpoint_close(struct na_sm_endpoint *na_sm_endpoint);

/**
 * Reserve queue pair.
 */
static na_return_t
na_sm_queue_pair_reserve(struct na_sm_region *na_sm_region, uint8_t *index);

/**
 * Release queue pair.
 */
static NA_INLINE void
na_sm_queue_pair_release(struct na_sm_region *na_sm_region, uint8_t index);

/**
 * Lookup addr key from map.
 */
static NA_INLINE struct na_sm_addr *
na_sm_addr_map_lookup(
    struct na_sm_map *na_sm_map, struct na_sm_addr_key *addr_key);

/**
 * Insert new addr key into map. Execute callback while write lock is acquired.
 */
static na_return_t
na_sm_addr_map_insert(struct na_sm_endpoint *na_sm_endpoint,
    struct na_sm_map *na_sm_map, const char *uri,
    struct na_sm_addr_key *addr_key, struct na_sm_addr **na_sm_addr_p);

/**
 * Remove addr key from map.
 */
static na_return_t
na_sm_addr_map_remove(
    struct na_sm_map *na_sm_map, struct na_sm_addr_key *addr_key);

/**
 * Create new address.
 */
static na_return_t
na_sm_addr_create(struct na_sm_endpoint *na_sm_endpoint, const char *uri,
    struct na_sm_addr_key *addr_key, bool unexpected,
    struct na_sm_addr **addr_p);

/**
 * Destroy address.
 */
static void
na_sm_addr_destroy(struct na_sm_addr *na_sm_addr);

/**
 * Increment ref count.
 */
static NA_INLINE void
na_sm_addr_ref_incr(struct na_sm_addr *na_sm_addr);

/**
 * Decrement ref count and free address if 0.
 */
static void
na_sm_addr_ref_decr(struct na_sm_addr *na_sm_addr);

/**
 * Resolve address.
 */
static na_return_t
na_sm_addr_resolve(struct na_sm_addr *na_sm_addr);

/**
 * Release address.
 */
static na_return_t
na_sm_addr_release(struct na_sm_addr *na_sm_addr);

/**
 * Send events as ancillary data.
 */
static na_return_t
na_sm_addr_event_send(int sock, const char *dest_name,
    union na_sm_cmd_hdr cmd_hdr, int tx_notify, int rx_notify,
    bool ignore_error);

/**
 * Recv events as ancillary data.
 */
static na_return_t
na_sm_addr_event_recv(int sock, union na_sm_cmd_hdr *cmd_hdr, int *tx_notify,
    int *rx_notify, bool *received);

/**
 * Send msg.
 */
static na_return_t
na_sm_msg_send(struct na_sm_class *na_sm_class, na_context_t *context,
    na_cb_type_t cb_type, na_cb_t callback, void *arg, const void *buf,
    size_t buf_size, struct na_sm_addr *na_sm_addr, na_tag_t tag,
    struct na_sm_op_id *na_sm_op_id);

/**
 * Post msg.
 */
static na_return_t
na_sm_msg_send_post(struct na_sm_endpoint *na_sm_endpoint, na_cb_type_t cb_type,
    const void *buf, size_t buf_size, struct na_sm_addr *na_sm_addr,
    na_tag_t tag);

/**
 * Reserve shared buffer.
 */
static NA_INLINE na_return_t
na_sm_buf_reserve(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int *index);

/**
 * Release shared buffer.
 */
static NA_INLINE void
na_sm_buf_release(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int index);

/**
 * Copy src to shared buffer.
 */
static NA_INLINE void
na_sm_buf_copy_to(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int index,
    const void *src, size_t n);

/**
 * Copy from shared buffer to dest.
 */
static NA_INLINE void
na_sm_buf_copy_from(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int index,
    void *dest, size_t n);

/**
 * RMA op.
 */
static na_return_t
na_sm_rma(struct na_sm_class *na_sm_class, na_context_t *context,
    na_cb_type_t cb_type, na_cb_t callback, void *arg,
    na_sm_process_vm_op_t process_vm_op,
    struct na_sm_mem_handle *na_sm_mem_handle_local, na_offset_t local_offset,
    struct na_sm_mem_handle *na_sm_mem_handle_remote, na_offset_t remote_offset,
    size_t length, struct na_sm_addr *na_sm_addr,
    struct na_sm_op_id *na_sm_op_id);

/**
 * Get IOV index and offset pair from an absolute offset.
 */
static NA_INLINE void
na_sm_iov_get_index_offset(const struct iovec *iov, unsigned long iovcnt,
    na_offset_t offset, unsigned long *iov_start_index,
    na_offset_t *iov_start_offset);

/**
 * Get IOV count for a given length.
 */
static NA_INLINE unsigned long
na_sm_iov_get_count(const struct iovec *iov, unsigned long iovcnt,
    unsigned long iov_start_index, na_offset_t iov_start_offset, size_t len);

/**
 * Create new IOV for transferring length data.
 */
static NA_INLINE void
na_sm_iov_translate(const struct iovec *iov, unsigned long iovcnt,
    unsigned long iov_start_index, na_offset_t iov_start_offset, size_t len,
    struct iovec *new_iov, unsigned long new_iovcnt);

/**
 * Wrapper for process_vm_writev().
 */
static na_return_t
na_sm_process_vm_writev(pid_t pid, const struct iovec *local_iov,
    unsigned long liovcnt, const struct iovec *remote_iov,
    unsigned long riovcnt, size_t length);

/**
 * Wrapper for process_vm_readv().
 */
static na_return_t
na_sm_process_vm_readv(pid_t pid, const struct iovec *local_iov,
    unsigned long liovcnt, const struct iovec *remote_iov,
    unsigned long riovcnt, size_t length);

/**
 * Poll waiting for timeout milliseconds.
 */
static na_return_t
na_sm_progress_wait(na_context_t *context,
    struct na_sm_endpoint *na_sm_endpoint, unsigned int timeout,
    unsigned int *count_p);

/**
 * Poll without waiting.
 */
static na_return_t
na_sm_progress(struct na_sm_endpoint *na_sm_endpoint, unsigned int *count_p);

/**
 * Progress on endpoint sock.
 */
static na_return_t
na_sm_progress_sock(struct na_sm_endpoint *na_sm_endpoint, bool *progressed);

/**
 * Progress cmd queue.
 */
static na_return_t
na_sm_progress_cmd_queue(
    struct na_sm_endpoint *na_sm_endpoint, bool *progressed);

/**
 * Process cmd.
 */
static na_return_t
na_sm_process_cmd(struct na_sm_endpoint *na_sm_endpoint,
    union na_sm_cmd_hdr cmd_hdr, int tx_notify, int rx_notify);

/**
 * Progress on tx notifications.
 */
static na_return_t
na_sm_progress_tx_notify(struct na_sm_addr *poll_addr, bool *progressed);

/**
 * Progress on rx notifications.
 */
static na_return_t
na_sm_progress_rx_notify(struct na_sm_addr *poll_addr, bool *progressed);

/**
 * Progress rx queue.
 */
static na_return_t
na_sm_progress_rx_queue(struct na_sm_endpoint *na_sm_endpoint,
    struct na_sm_addr *poll_addr, bool *progressed);

/**
 * Process unexpected messages.
 */
static na_return_t
na_sm_process_unexpected(struct na_sm_op_queue *unexpected_op_queue,
    struct na_sm_addr *poll_addr, union na_sm_msg_hdr msg_hdr,
    struct na_sm_unexpected_msg_queue *unexpected_msg_queue);

/**
 * Process expected messages.
 */
static void
na_sm_process_expected(struct na_sm_op_queue *expected_op_queue,
    struct na_sm_addr *poll_addr, union na_sm_msg_hdr msg_hdr);

/**
 * Process retries.
 */
static na_return_t
na_sm_process_retries(struct na_sm_endpoint *na_sm_endpoint);

/**
 * Push operation for retry.
 */
static NA_INLINE void
na_sm_op_retry(
    struct na_sm_class *na_sm_class, struct na_sm_op_id *na_sm_op_id);

/**
 * Complete operation.
 */
static NA_INLINE void
na_sm_complete(struct na_sm_op_id *na_sm_op_id, na_return_t cb_ret);

/**
 * Signal internal completion.
 */
static NA_INLINE void
na_sm_complete_signal(struct na_sm_class *na_sm_class);

/**
 * Release memory.
 */
static NA_INLINE void
na_sm_release(void *arg);

/* get_protocol_info */
static na_return_t
na_sm_get_protocol_info(const struct na_info *na_info,
    struct na_protocol_info **na_protocol_info_p);

/* check_protocol */
static bool
na_sm_check_protocol(const char *protocol_name);

/* initialize */
static na_return_t
na_sm_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen);

/* finalize */
static na_return_t
na_sm_finalize(na_class_t *na_class);

/* context_create */
static na_return_t
na_sm_context_create(na_class_t *na_class, void **context_p, uint8_t id);

/* context_destroy */
static na_return_t
na_sm_context_destroy(na_class_t *na_class, void *context);

/* cleanup */
static void
na_sm_cleanup(void);

/* op_create */
static na_op_id_t *
na_sm_op_create(na_class_t *na_class, unsigned long flags);

/* op_destroy */
static void
na_sm_op_destroy(na_class_t *na_class, na_op_id_t *op_id);

/* addr_lookup */
static na_return_t
na_sm_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr_p);

/* addr_free */
static void
na_sm_addr_free(na_class_t *na_class, na_addr_t *addr);

/* addr_self */
static na_return_t
na_sm_addr_self(na_class_t *na_class, na_addr_t **addr_p);

/* addr_dup */
static na_return_t
na_sm_addr_dup(na_class_t *na_class, na_addr_t *addr, na_addr_t **new_addr_p);

/* addr_cmp */
static bool
na_sm_addr_cmp(na_class_t *na_class, na_addr_t *addr1, na_addr_t *addr2);

/* addr_is_self */
static NA_INLINE bool
na_sm_addr_is_self(na_class_t *na_class, na_addr_t *addr);

/* addr_to_string */
static na_return_t
na_sm_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr);

/* addr_get_serialize_size */
static NA_INLINE size_t
na_sm_addr_get_serialize_size(na_class_t *na_class, na_addr_t *addr);

/* addr_serialize */
static na_return_t
na_sm_addr_serialize(
    na_class_t *na_class, void *buf, size_t buf_size, na_addr_t *addr);

/* addr_deserialize */
static na_return_t
na_sm_addr_deserialize(
    na_class_t *na_class, na_addr_t **addr_p, const void *buf, size_t buf_size);

/* msg_get_max_unexpected_size */
static NA_INLINE size_t
na_sm_msg_get_max_unexpected_size(const na_class_t *na_class);

/* msg_get_max_expected_size */
static NA_INLINE size_t
na_sm_msg_get_max_expected_size(const na_class_t *na_class);

/* msg_get_max_tag */
static NA_INLINE na_tag_t
na_sm_msg_get_max_tag(const na_class_t *na_class);

/* msg_send_unexpected */
static na_return_t
na_sm_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_unexpected */
static na_return_t
na_sm_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id);

/* msg_send_expected */
static na_return_t
na_sm_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_expected */
static na_return_t
na_sm_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_addr_t *source_addr, uint8_t source_id, na_tag_t tag, na_op_id_t *op_id);

/* mem_handle_create */
static na_return_t
na_sm_mem_handle_create(na_class_t *na_class, void *buf, size_t buf_size,
    unsigned long flags, na_mem_handle_t **mem_handle_p);

#ifdef NA_SM_HAS_CMA
/* mem_handle_create_segments */
static na_return_t
na_sm_mem_handle_create_segments(na_class_t *na_class,
    struct na_segment *segments, size_t segment_count, unsigned long flags,
    na_mem_handle_t **mem_handle_p);
#endif

/* mem_handle_free */
static void
na_sm_mem_handle_free(na_class_t *na_class, na_mem_handle_t *mem_handle);

/* mem_handle_get_max_segments */
static size_t
na_sm_mem_handle_get_max_segments(const na_class_t *na_class);

/* mem_handle_get_serialize_size */
static NA_INLINE size_t
na_sm_mem_handle_get_serialize_size(
    na_class_t *na_class, na_mem_handle_t *mem_handle);

/* mem_handle_serialize */
static na_return_t
na_sm_mem_handle_serialize(na_class_t *na_class, void *buf, size_t buf_size,
    na_mem_handle_t *mem_handle);

/* mem_handle_deserialize */
static na_return_t
na_sm_mem_handle_deserialize(na_class_t *na_class,
    na_mem_handle_t **mem_handle_p, const void *buf, size_t buf_size);

/* put */
static NA_INLINE na_return_t
na_sm_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* get */
static NA_INLINE na_return_t
na_sm_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* poll_get_fd */
static NA_INLINE int
na_sm_poll_get_fd(na_class_t *na_class, na_context_t *context);

/* poll_try_wait */
static NA_INLINE bool
na_sm_poll_try_wait(na_class_t *na_class, na_context_t *context);

/* poll */
static na_return_t
na_sm_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p);

/* poll_wait */
static na_return_t
na_sm_poll_wait(na_class_t *na_class, na_context_t *context,
    unsigned int timeout, unsigned int *count_p);

/* cancel */
static na_return_t
na_sm_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t *op_id);

/*******************/
/* Local Variables */
/*******************/

const struct na_class_ops NA_PLUGIN_OPS(sm) = {
    "na",                              /* name */
    na_sm_get_protocol_info,           /* get_protocol_info */
    na_sm_check_protocol,              /* check_protocol */
    na_sm_initialize,                  /* initialize */
    na_sm_finalize,                    /* finalize */
    na_sm_cleanup,                     /* cleanup */
    NULL,                              /* has_opt_feature */
    na_sm_context_create,              /* context_create */
    na_sm_context_destroy,             /* context_destroy */
    na_sm_op_create,                   /* op_create */
    na_sm_op_destroy,                  /* op_destroy */
    na_sm_addr_lookup,                 /* addr_lookup */
    na_sm_addr_free,                   /* addr_free */
    NULL,                              /* addr_set_remove */
    na_sm_addr_self,                   /* addr_self */
    na_sm_addr_dup,                    /* addr_dup */
    na_sm_addr_cmp,                    /* addr_cmp */
    na_sm_addr_is_self,                /* addr_is_self */
    na_sm_addr_to_string,              /* addr_to_string */
    na_sm_addr_get_serialize_size,     /* addr_get_serialize_size */
    na_sm_addr_serialize,              /* addr_serialize */
    na_sm_addr_deserialize,            /* addr_deserialize */
    na_sm_msg_get_max_unexpected_size, /* msg_get_max_unexpected_size */
    na_sm_msg_get_max_expected_size,   /* msg_get_max_expected_size */
    NULL,                              /* msg_get_unexpected_header_size */
    NULL,                              /* msg_get_expected_header_size */
    na_sm_msg_get_max_tag,             /* msg_get_max_tag */
    NULL,                              /* msg_buf_alloc */
    NULL,                              /* msg_buf_free */
    NULL,                              /* msg_init_unexpected */
    na_sm_msg_send_unexpected,         /* msg_send_unexpected */
    na_sm_msg_recv_unexpected,         /* msg_recv_unexpected */
    NULL,                              /* msg_multi_recv_unexpected */
    NULL,                              /* msg_init_expected */
    na_sm_msg_send_expected,           /* msg_send_expected */
    na_sm_msg_recv_expected,           /* msg_recv_expected */
    na_sm_mem_handle_create,           /* mem_handle_create */
#ifdef NA_SM_HAS_CMA
    na_sm_mem_handle_create_segments, /* mem_handle_create_segments */
#else
    NULL, /* mem_handle_create_segments */
#endif
    na_sm_mem_handle_free,               /* mem_handle_free */
    na_sm_mem_handle_get_max_segments,   /* mem_handle_get_max_segments */
    NULL,                                /* mem_register */
    NULL,                                /* mem_deregister */
    na_sm_mem_handle_get_serialize_size, /* mem_handle_get_serialize_size */
    na_sm_mem_handle_serialize,          /* mem_handle_serialize */
    na_sm_mem_handle_deserialize,        /* mem_handle_deserialize */
    na_sm_put,                           /* put */
    na_sm_get,                           /* get */
    na_sm_poll_get_fd,                   /* poll_get_fd */
    na_sm_poll_try_wait,                 /* poll_try_wait */
    na_sm_poll,                          /* poll */
    na_sm_poll_wait,                     /* poll_wait */
    na_sm_cancel                         /* cancel */
};

/********************/
/* Plugin callbacks */
/********************/

/* Debug information */
#ifdef NA_HAS_DEBUG
static char *
lltoa(uint64_t val, char *string, int radix)
{
    int i = sizeof(val) * 8;

    for (; val && i; --i, val /= (uint64_t) radix)
        string[i - 1] = "0123456789abcdef"[val % (uint64_t) radix];

    return &string[i];
}
#endif

/*---------------------------------------------------------------------------*/
na_return_t
NA_SM_Host_id_get(na_sm_id_t *id_p)
{
#ifdef NA_SM_HAS_UUID
    char uuid_str[NA_SM_HOST_ID_LEN + 1];
    FILE *uuid_config = NULL;
    uuid_t new_uuid;
    char pathname[NA_SM_MAX_FILENAME] = {'\0'};
    na_return_t ret = NA_SUCCESS;
    int rc;

    rc = snprintf(pathname, NA_SM_MAX_FILENAME, "%s/%s_uuid.cfg",
        NA_SM_TMP_DIRECTORY, NA_SM_SHM_PREFIX);
    NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > NA_SM_MAX_FILENAME, done, ret,
        NA_OVERFLOW, "snprintf() failed, rc: %d", rc);

    uuid_config = fopen(pathname, "r");
    if (!uuid_config) {
        /* Generate a new one */
        uuid_generate(new_uuid);

        uuid_config = fopen(pathname, "w");
        NA_CHECK_SUBSYS_ERROR(addr, uuid_config == NULL, done, ret,
            na_sm_errno_to_na(errno), "Could not open %s for write (%s)",
            pathname, strerror(errno));
        uuid_unparse(new_uuid, uuid_str);
        fprintf(uuid_config, "%s\n", uuid_str);
    } else {
        /* Get the existing one */
        fgets(uuid_str, NA_SM_HOST_ID_LEN + 1, uuid_config);
        uuid_parse(uuid_str, new_uuid);
    }
    fclose(uuid_config);
    uuid_copy(*id_p, new_uuid);

done:
    return ret;
#else
    *id_p = gethostid();

    return NA_SUCCESS;
#endif
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_SM_Host_id_to_string(na_sm_id_t id, char *string)
{
#ifdef NA_SM_HAS_UUID
    uuid_unparse(id, string);

    return NA_SUCCESS;
#else
    na_return_t ret = NA_SUCCESS;
    int rc = snprintf(string, NA_SM_HOST_ID_LEN + 1, "%ld", id);
    NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > NA_SM_HOST_ID_LEN + 1, done, ret,
        NA_OVERFLOW, "snprintf() failed, rc: %d", rc);

done:
    return ret;
#endif
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_SM_String_to_host_id(const char *string, na_sm_id_t *id_p)
{
#ifdef NA_SM_HAS_UUID
    return (uuid_parse(string, *id_p) == 0) ? NA_SUCCESS : NA_PROTOCOL_ERROR;
#else
    na_return_t ret = NA_SUCCESS;
    int rc = sscanf(string, "%ld", id_p);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, done, ret, NA_PROTOCOL_ERROR,
        "sscanf() failed, rc: %d", rc);

done:
    return ret;
#endif
}

/*---------------------------------------------------------------------------*/
void
NA_SM_Host_id_copy(na_sm_id_t *dst_p, na_sm_id_t src)
{
#ifdef NA_SM_HAS_UUID
    uuid_copy(*dst_p, src);
#else
    *dst_p = src;
#endif
}

/*---------------------------------------------------------------------------*/
bool
NA_SM_Host_id_cmp(na_sm_id_t id1, na_sm_id_t id2)
{
#ifdef NA_SM_HAS_UUID
    return (uuid_compare(id1, id2) == 0) ? true : false;
#else
    return (id1 == id2);
#endif
}

#ifdef NA_SM_HAS_CMA
/*---------------------------------------------------------------------------*/
static int
na_sm_get_ptrace_scope_value(void)
{
    FILE *file;
    int val = 0, rc;

    /* Try to open ptrace_scope */
    file = fopen("/proc/sys/kernel/yama/ptrace_scope", "r");
    if (file) {
        rc = fscanf(file, "%d", &val);
        NA_CHECK_SUBSYS_ERROR_NORET(
            cls, rc != 1, done, "Could not get value from ptrace_scope");

        rc = fclose(file);
        NA_CHECK_SUBSYS_ERROR_NORET(
            cls, rc != 0, done, "fclose() failed (%s)", strerror(errno));
    }

done:
    return val;
}
#endif

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_errno_to_na(int rc)
{
    na_return_t ret;

    switch (rc) {
        case EPERM:
            ret = NA_PERMISSION;
            break;
        case ENOENT:
            ret = NA_NOENTRY;
            break;
        case EINTR:
            ret = NA_INTERRUPT;
            break;
        case EAGAIN:
            ret = NA_AGAIN;
            break;
        case ENOMEM:
            ret = NA_NOMEM;
            break;
        case EACCES:
            ret = NA_ACCESS;
            break;
        case EFAULT:
            ret = NA_FAULT;
            break;
        case EBUSY:
            ret = NA_BUSY;
            break;
        case EEXIST:
            ret = NA_EXIST;
            break;
        case ENODEV:
            ret = NA_NODEV;
            break;
        case EINVAL:
            ret = NA_INVALID_ARG;
            break;
        case EOVERFLOW:
        case ENAMETOOLONG:
            ret = NA_OVERFLOW;
            break;
        case EMSGSIZE:
            ret = NA_MSGSIZE;
            break;
        case EPROTONOSUPPORT:
            ret = NA_PROTONOSUPPORT;
            break;
        case EOPNOTSUPP:
            ret = NA_OPNOTSUPPORTED;
            break;
        case EADDRINUSE:
            ret = NA_ADDRINUSE;
            break;
        case EADDRNOTAVAIL:
            ret = NA_ADDRNOTAVAIL;
            break;
        case ETIMEDOUT:
            ret = NA_TIMEOUT;
            break;
        case ECANCELED:
            ret = NA_CANCELED;
            break;
        default:
            ret = NA_PROTOCOL_ERROR;
            break;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static void *
na_sm_shm_map(const char *name, size_t length, bool create)
{
    size_t page_size = (size_t) hg_mem_get_page_size();

    /* Check alignment */
    NA_CHECK_SUBSYS_WARNING(mem, length / page_size * page_size != length,
        "Not aligned properly, page size=%zu bytes, length=%zu bytes",
        page_size, length);

    return hg_mem_shm_map(name, length, create);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_shm_unmap(const char *name, void *addr, size_t length)
{
    return (hg_mem_shm_unmap(name, addr, length) == HG_UTIL_SUCCESS)
               ? NA_SUCCESS
               : na_sm_errno_to_na(errno);
}

/*---------------------------------------------------------------------------*/
static int
na_sm_shm_cleanup(const char *fpath, const struct stat NA_UNUSED *sb,
    int NA_UNUSED typeflag, struct FTW NA_UNUSED *ftwbuf)
{
    const char *prefix = NA_SM_SHM_PATH "/" NA_SM_SHM_PREFIX "-";
    int ret = 0;

    if (strncmp(fpath, prefix, strlen(prefix)) == 0) {
        const char *shm_name = fpath + strlen(NA_SM_SHM_PATH "/");

        NA_LOG_SUBSYS_DEBUG(mem, "shm_unmap() %s", shm_name);
        ret = hg_mem_shm_unmap(shm_name, NULL, 0);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_msg_queue_init(struct na_sm_msg_queue *na_sm_queue)
{
    unsigned int count = NA_SM_NUM_BUFS;

    na_sm_queue->prod_size = na_sm_queue->cons_size = count;
    na_sm_queue->prod_mask = na_sm_queue->cons_mask = count - 1;
    hg_atomic_init32(&na_sm_queue->prod_head, 0);
    hg_atomic_init32(&na_sm_queue->cons_head, 0);
    hg_atomic_init32(&na_sm_queue->prod_tail, 0);
    hg_atomic_init32(&na_sm_queue->cons_tail, 0);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_sm_msg_queue_push(
    struct na_sm_msg_queue *na_sm_queue, const union na_sm_msg_hdr *msg_hdr)
{
    int32_t prod_head, prod_next, cons_tail;

    do {
        prod_head = hg_atomic_get32(&na_sm_queue->prod_head);
        prod_next = (prod_head + 1) & (int) na_sm_queue->prod_mask;
        cons_tail = hg_atomic_get32(&na_sm_queue->cons_tail);

        if (prod_next == cons_tail) {
            hg_atomic_fence();
            if (prod_head == hg_atomic_get32(&na_sm_queue->prod_head) &&
                cons_tail == hg_atomic_get32(&na_sm_queue->cons_tail)) {
                na_sm_queue->drops++;
                /* Full */
                return false;
            }
            continue;
        }
    } while (!hg_atomic_cas32(&na_sm_queue->prod_head, prod_head, prod_next));

    hg_atomic_set64(&na_sm_queue->ring[prod_head], (int64_t) msg_hdr->val);

    /*
     * If there are other enqueues in progress
     * that preceded us, we need to wait for them
     * to complete
     */
    while (hg_atomic_get32(&na_sm_queue->prod_tail) != prod_head)
        cpu_spinwait();

    hg_atomic_set32(&na_sm_queue->prod_tail, prod_next);

    return true;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_sm_msg_queue_pop(
    struct na_sm_msg_queue *na_sm_queue, union na_sm_msg_hdr *msg_hdr)
{
    int32_t cons_head, cons_next;

    do {
        cons_head = hg_atomic_get32(&na_sm_queue->cons_head);
        cons_next = (cons_head + 1) & (int) na_sm_queue->cons_mask;

        if (cons_head == hg_atomic_get32(&na_sm_queue->prod_tail))
            return false;
    } while (!hg_atomic_cas32(&na_sm_queue->cons_head, cons_head, cons_next));

    msg_hdr->val = (uint64_t) hg_atomic_get64(&na_sm_queue->ring[cons_head]);

    /*
     * If there are other dequeues in progress
     * that preceded us, we need to wait for them
     * to complete
     */
    while (hg_atomic_get32(&na_sm_queue->cons_tail) != cons_head)
        cpu_spinwait();

    hg_atomic_set32(&na_sm_queue->cons_tail, cons_next);

    return true;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_sm_msg_queue_is_empty(struct na_sm_msg_queue *na_sm_queue)
{
    return (hg_atomic_get32(&na_sm_queue->cons_head) ==
            hg_atomic_get32(&na_sm_queue->prod_tail));
}

/*---------------------------------------------------------------------------*/
static void
na_sm_cmd_queue_init(struct na_sm_cmd_queue *na_sm_queue)
{
    unsigned int count = NA_SM_MAX_PEERS * 2;

    na_sm_queue->prod_size = na_sm_queue->cons_size = count;
    na_sm_queue->prod_mask = na_sm_queue->cons_mask = count - 1;
    hg_atomic_init32(&na_sm_queue->prod_head, 0);
    hg_atomic_init32(&na_sm_queue->cons_head, 0);
    hg_atomic_init32(&na_sm_queue->prod_tail, 0);
    hg_atomic_init32(&na_sm_queue->cons_tail, 0);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_sm_cmd_queue_push(
    struct na_sm_cmd_queue *na_sm_queue, const union na_sm_cmd_hdr *cmd_hdr)
{
    int32_t prod_head, prod_next, cons_tail;

    do {
        prod_head = hg_atomic_get32(&na_sm_queue->prod_head);
        prod_next = (prod_head + 1) & (int) na_sm_queue->prod_mask;
        cons_tail = hg_atomic_get32(&na_sm_queue->cons_tail);

        if (prod_next == cons_tail) {
            hg_atomic_fence();
            if (prod_head == hg_atomic_get32(&na_sm_queue->prod_head) &&
                cons_tail == hg_atomic_get32(&na_sm_queue->cons_tail)) {
                na_sm_queue->drops++;
                /* Full */
                return false;
            }
            continue;
        }
    } while (!hg_atomic_cas32(&na_sm_queue->prod_head, prod_head, prod_next));

    hg_atomic_set64(&na_sm_queue->ring[prod_head], (int64_t) cmd_hdr->val);

    /*
     * If there are other enqueues in progress
     * that preceded us, we need to wait for them
     * to complete
     */
    while (hg_atomic_get32(&na_sm_queue->prod_tail) != prod_head)
        cpu_spinwait();

    hg_atomic_set32(&na_sm_queue->prod_tail, prod_next);

    return true;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_sm_cmd_queue_pop(
    struct na_sm_cmd_queue *na_sm_queue, union na_sm_cmd_hdr *cmd_hdr)
{
    int32_t cons_head, cons_next;

    do {
        cons_head = hg_atomic_get32(&na_sm_queue->cons_head);
        cons_next = (cons_head + 1) & (int) na_sm_queue->cons_mask;

        if (cons_head == hg_atomic_get32(&na_sm_queue->prod_tail))
            return false;
    } while (!hg_atomic_cas32(&na_sm_queue->cons_head, cons_head, cons_next));

    cmd_hdr->val = (uint64_t) hg_atomic_get64(&na_sm_queue->ring[cons_head]);

    /*
     * If there are other dequeues in progress
     * that preceded us, we need to wait for them
     * to complete
     */
    while (hg_atomic_get32(&na_sm_queue->cons_tail) != cons_head)
        cpu_spinwait();

    hg_atomic_set32(&na_sm_queue->cons_tail, cons_next);

    return true;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_sm_addr_key_hash(hg_hash_table_key_t key)
{
    /* Hashing through PIDs should be sufficient in practice */
    return (unsigned int) ((struct na_sm_addr_key *) key)->pid;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_sm_addr_key_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    struct na_sm_addr_key *addr_key1 = (struct na_sm_addr_key *) key1,
                          *addr_key2 = (struct na_sm_addr_key *) key2;

    return (addr_key1->pid == addr_key2->pid && addr_key1->id == addr_key2->id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_string_to_addr(
    const char *str, char *uri, size_t size, struct na_sm_addr_key *addr_key_p)
{
    na_return_t ret = NA_SUCCESS;
    char *uri_start;
    const char *delim = "://";
    int rc;

    /* Keep URI */
    uri_start = strstr(str, delim);
    NA_CHECK_SUBSYS_ERROR(addr, uri_start == NULL, done, ret, NA_INVALID_ARG,
        "Malformed address string (%s)", str);
    strncpy(uri, uri_start + strlen(delim), size - 1);

    /* Get PID / ID from name */
    rc = NA_SM_SCAN_URI(uri, addr_key_p);
    if (rc != 2) {
        /* Try to retrieve address key from region */
        ret = na_sm_region_get_addr_key(uri, addr_key_p);
        NA_CHECK_SUBSYS_NA_ERROR(addr, done, ret,
            "Could not retrieve address key from URI (%s)", uri);
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_region_open(const char *uri, bool create, struct na_sm_region **region_p)
{
    char filename[NA_SM_MAX_FILENAME];
    struct na_sm_region *na_sm_region = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* Generate SHM object name */
    rc = NA_SM_PRINT_SHM_NAME(filename, NA_SM_MAX_FILENAME, uri);
    NA_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > NA_SM_MAX_FILENAME, done, ret,
        NA_OVERFLOW, "NA_SM_PRINT_SHM_NAME() failed, rc: %d", rc);

    /* Open SHM object */
    NA_LOG_SUBSYS_DEBUG(cls, "shm_map() %s", filename);
    na_sm_region = (struct na_sm_region *) na_sm_shm_map(
        filename, sizeof(struct na_sm_region), create);
    NA_CHECK_SUBSYS_ERROR(cls, na_sm_region == NULL, done, ret, NA_NODEV,
        "Could not map new SM region (%s)", filename);

    if (create) {
        int i;

        /* Initialize copy buf (all buffers are available by default) */
        hg_atomic_init64(
            &na_sm_region->copy_bufs.available.val, ~((int64_t) 0));
        memset(&na_sm_region->copy_bufs.buf, 0,
            sizeof(na_sm_region->copy_bufs.buf));

        /* Initialize locks */
        for (i = 0; i < NA_SM_NUM_BUFS; i++)
            hg_thread_spin_init(&na_sm_region->copy_bufs.buf_locks[i]);

        /* Initialize queue pairs */
        for (i = 0; i < 4; i++)
            hg_atomic_init64(&na_sm_region->available.val[i], ~((int64_t) 0));

        for (i = 0; i < NA_SM_MAX_PEERS; i++) {
            na_sm_msg_queue_init(&na_sm_region->queue_pairs[i].rx_queue);
            na_sm_msg_queue_init(&na_sm_region->queue_pairs[i].tx_queue);
        }

        /* Initialize command queue */
        na_sm_cmd_queue_init(&na_sm_region->cmd_queue);
    }

    *region_p = na_sm_region;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_region_close(const char *uri, struct na_sm_region *region)
{
    char filename[NA_SM_MAX_FILENAME];
    char *filename_p = NULL;
    na_return_t ret = NA_SUCCESS;

    if (uri) {
        /* Generate SHM object name */
        int rc = NA_SM_PRINT_SHM_NAME(filename, NA_SM_MAX_FILENAME, uri);
        NA_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > NA_SM_MAX_FILENAME, done, ret,
            NA_OVERFLOW, "NA_SM_PRINT_SHM_NAME() failed, rc: %d", rc);
        filename_p = filename;
    }

    NA_LOG_SUBSYS_DEBUG(
        cls, "shm_unmap() %s", (filename_p == NULL) ? "is NULL" : filename_p);
    ret = na_sm_shm_unmap(filename_p, region, sizeof(struct na_sm_region));
    NA_CHECK_SUBSYS_NA_ERROR(cls, done, ret, "Could not unmap SM region (%s)",
        (filename_p == NULL) ? "is NULL" : filename_p);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_region_get_addr_key(const char *uri, struct na_sm_addr_key *addr_key_p)
{
    char filename[NA_SM_MAX_FILENAME];
    struct na_sm_region *na_sm_region = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* Generate SHM object name */
    rc = NA_SM_PRINT_SHM_NAME(filename, NA_SM_MAX_FILENAME, uri);
    NA_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > NA_SM_MAX_FILENAME, done, ret,
        NA_OVERFLOW, "NA_SM_PRINT_SHM_NAME() failed, rc: %d", rc);

    /* Open SHM object */
    NA_LOG_SUBSYS_DEBUG(cls, "shm_map() %s", filename);
    na_sm_region = (struct na_sm_region *) na_sm_shm_map(
        filename, sizeof(struct na_sm_region), false);
    NA_CHECK_SUBSYS_ERROR(cls, na_sm_region == NULL, done, ret, NA_NODEV,
        "Could not map SM region (%s)", filename);

    /* Copy addr_key */
    *addr_key_p = na_sm_region->addr_key;

    /* Close SHM object */
    ret = na_sm_shm_unmap(NULL, na_sm_region, sizeof(struct na_sm_region));
    NA_CHECK_SUBSYS_NA_ERROR(cls, done, ret, "Could not unmap SM region");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_sock_open(const char *uri, bool create, int *sock)
{
    int socket_type = SOCK_DGRAM, /* reliable with AF_UNIX */
        fd = -1, rc;
    char pathname[NA_SM_MAX_FILENAME];
    bool created_sock_path = false;
    na_return_t ret = NA_SUCCESS;

    /* Create a non-blocking socket so that we can poll for incoming connections
     */
#ifdef SOCK_NONBLOCK
    socket_type |= SOCK_NONBLOCK;
#endif
    fd = socket(AF_UNIX, socket_type, 0);
    NA_CHECK_SUBSYS_ERROR(cls, fd == -1, error, ret, na_sm_errno_to_na(errno),
        "socket() failed (%s)", strerror(errno));

#ifndef SOCK_NONBLOCK
    rc = fcntl(fd, F_SETFL, O_NONBLOCK);
    NA_CHECK_SUBSYS_ERROR(cls, rc == -1, error, ret, na_sm_errno_to_na(errno),
        "fcntl() failed (%s)", strerror(errno));
#endif

    if (create) {
        struct sockaddr_un addr;

        /* Generate named socket path */
        rc = NA_SM_PRINT_SOCK_PATH(pathname, NA_SM_MAX_FILENAME, uri);
        NA_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > NA_SM_MAX_FILENAME, error,
            ret, NA_OVERFLOW, "NA_SM_PRINT_SOCK_PATH() failed, rc: %d", rc);

        memset(&addr, 0, sizeof(struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        NA_CHECK_SUBSYS_ERROR(cls,
            strlen(pathname) + strlen(NA_SM_SOCK_NAME) >
                sizeof(addr.sun_path) - 1,
            error, ret, NA_OVERFLOW,
            "Exceeds maximum AF UNIX socket path length");
        strcpy(addr.sun_path, pathname);
        strcat(addr.sun_path, NA_SM_SOCK_NAME);

        /* Create path */
        ret = na_sm_sock_path_create(pathname);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not create socket path (%s)", pathname);
        created_sock_path = true;

        /* Bind and create named socket */
        NA_LOG_SUBSYS_DEBUG(cls, "bind() %s", addr.sun_path);
        rc = bind(
            fd, (const struct sockaddr *) &addr, (socklen_t) SUN_LEN(&addr));
        NA_CHECK_SUBSYS_ERROR(cls, rc == -1, error, ret,
            na_sm_errno_to_na(errno), "bind() failed (%s)", strerror(errno));
    }

    *sock = fd;

    return ret;

error:
    if (fd != -1) {
        rc = close(fd);
        NA_CHECK_SUBSYS_ERROR_DONE(
            cls, rc == -1, "close() failed (%s)", strerror(errno));
    }
    if (created_sock_path) {
        na_return_t err_ret = na_sm_sock_path_remove(pathname);
        NA_CHECK_SUBSYS_ERROR_DONE(cls, err_ret != NA_SUCCESS,
            "na_sm_remove_sock_path() failed (%s)", pathname);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_sock_close(const char *uri, int sock)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    NA_LOG_SUBSYS_DEBUG(cls, "Closing sock %d", sock);
    rc = close(sock);
    NA_CHECK_SUBSYS_ERROR(cls, rc == -1, done, ret, na_sm_errno_to_na(errno),
        "close() failed (%s)", strerror(errno));

    if (uri) {
        char pathname[NA_SM_MAX_FILENAME];
        struct sockaddr_un addr;

        rc = NA_SM_PRINT_SOCK_PATH(pathname, NA_SM_MAX_FILENAME, uri);
        NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > NA_SM_MAX_FILENAME, done,
            ret, NA_OVERFLOW, "NA_SM_PRINT_SOCK_PATH() failed, rc: %d", rc);
        strcpy(addr.sun_path, pathname);
        strcat(addr.sun_path, NA_SM_SOCK_NAME);

        NA_LOG_SUBSYS_DEBUG(cls, "unlink() %s", addr.sun_path);
        rc = unlink(addr.sun_path);
        NA_CHECK_SUBSYS_ERROR(cls, rc == -1, done, ret,
            na_sm_errno_to_na(errno), "unlink() failed (%s)", strerror(errno));

        ret = na_sm_sock_path_remove(pathname);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, done, ret, "Could not remove %s path", pathname);
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_sock_path_create(const char *pathname)
{
    char *dup_path = NULL, *path_ptr;
    char stat_path[NA_SM_MAX_FILENAME] = {'\0'};
    na_return_t ret = NA_SUCCESS;

    dup_path = strdup(pathname);
    NA_CHECK_SUBSYS_ERROR(
        cls, dup_path == NULL, done, ret, NA_NOMEM, "Could not dup pathname");
    path_ptr = dup_path;

    /* Skip leading '/' */
    if (dup_path[0] == '/') {
        path_ptr++;
        stat_path[0] = '/';
    }

    /* Create path */
    while (path_ptr != NULL) {
        char *current = strtok_r(path_ptr, "/", &path_ptr);
        struct stat sb;

        if (!current)
            break;

        strcat(stat_path, current);
        if (stat(stat_path, &sb) == -1) {
            int rc;
            NA_LOG_SUBSYS_DEBUG(cls, "mkdir %s", stat_path);
            rc = mkdir(stat_path, 0775);
            NA_CHECK_SUBSYS_ERROR(cls, rc == -1 && errno != EEXIST, done, ret,
                na_sm_errno_to_na(errno), "Could not create directory: %s (%s)",
                stat_path, strerror(errno));
        }
        strcat(stat_path, "/");
    }

done:
    free(dup_path);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_sock_path_remove(const char *pathname)
{
    char dup_path[NA_SM_MAX_FILENAME] = {'\0'};
    char *path_ptr = NULL;
    na_return_t ret = NA_SUCCESS;

    strcpy(dup_path, pathname);

    /* Delete path */
    path_ptr = strrchr(dup_path, '/');
    while (path_ptr) {
        NA_LOG_SUBSYS_DEBUG(cls, "rmdir %s", dup_path);
        if (rmdir(dup_path) == -1) {
            /* Silently ignore */
        }
        *path_ptr = '\0';
        path_ptr = strrchr(dup_path, '/');
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static int
na_sm_sock_path_cleanup(const char *fpath, const struct stat NA_UNUSED *sb,
    int NA_UNUSED typeflag, struct FTW NA_UNUSED *ftwbuf)
{
    const char *prefix = NA_SM_TMP_DIRECTORY "/" NA_SM_SHM_PREFIX "-";
    int ret = 0;

    if (strncmp(fpath, prefix, strlen(prefix)) == 0)
        ret = remove(fpath);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_event_create(const char NA_UNUSED *uri, uint8_t NA_UNUSED pair_index,
    unsigned char NA_UNUSED pair, int *event)
{
    na_return_t ret = NA_SUCCESS;
    int fd = -1;

#ifdef HG_UTIL_HAS_SYSEVENTFD_H
    fd = hg_event_create();
    NA_CHECK_SUBSYS_ERROR(ctx, fd == -1, error, ret, na_sm_errno_to_na(errno),
        "hg_event_create() failed");
#else
    char fifo_name[NA_SM_MAX_FILENAME] = {'\0'};
    int rc;

    /**
     * If eventfd is not supported, we need to explicitly use named pipes in
     * this case as kqueue file descriptors cannot be exchanged through
     * ancillary data.
     */
    rc = NA_SM_PRINT_FIFO_NAME(
        fifo_name, NA_SM_MAX_FILENAME, uri, pair_index, pair);
    NA_CHECK_SUBSYS_ERROR(ctx, rc < 0 || rc > NA_SM_MAX_FILENAME, error, ret,
        NA_OVERFLOW, "NA_SM_PRINT_FIFO_NAME() failed, rc: %d", rc);

    /* Create FIFO */
    NA_LOG_SUBSYS_DEBUG(ctx, "mkfifo() %s", fifo_name);
    rc = mkfifo(fifo_name, S_IRUSR | S_IWUSR);
    NA_CHECK_SUBSYS_ERROR(ctx, rc == -1, error, ret, na_sm_errno_to_na(errno),
        "mkfifo() failed (%s)", strerror(errno));

    /* Open FIFO (RDWR for convenience) */
    fd = open(fifo_name, O_RDWR);
    NA_CHECK_SUBSYS_ERROR(ctx, fd == -1, error, ret, na_sm_errno_to_na(errno),
        "open() failed (%s)", strerror(errno));

    /* Set FIFO to be non-blocking */
    rc = fcntl(fd, F_SETFL, O_NONBLOCK);
    NA_CHECK_SUBSYS_ERROR(ctx, rc == -1, error, ret, na_sm_errno_to_na(errno),
        "fcntl() failed (%s)", strerror(errno));
#endif
    NA_LOG_SUBSYS_DEBUG(ctx, "Created event %d", fd);

    *event = fd;

    return ret;

error:
#ifndef HG_UTIL_HAS_SYSEVENTFD_H
    if (fd != -1) {
        rc = close(fd);
        NA_CHECK_SUBSYS_ERROR_DONE(
            ctx, rc == -1, "close() failed (%s)", strerror(errno));
    }
#endif

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_event_destroy(const char NA_UNUSED *uri, uint8_t NA_UNUSED pair_index,
    unsigned char NA_UNUSED pair, bool NA_UNUSED remove, int event)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    NA_LOG_SUBSYS_DEBUG(ctx, "Closing event %d", event);
#ifdef HG_UTIL_HAS_SYSEVENTFD_H
    rc = hg_event_destroy(event);
    NA_CHECK_SUBSYS_ERROR(ctx, rc == HG_UTIL_FAIL, done, ret,
        na_sm_errno_to_na(errno), "hg_event_destroy() failed");
#else
    rc = close(event);
    NA_CHECK_SUBSYS_ERROR(ctx, rc == -1, done, ret, na_sm_errno_to_na(errno),
        "close() failed (%s)", strerror(errno));

    if (remove) {
        char fifo_name[NA_SM_MAX_FILENAME] = {'\0'};

        rc = NA_SM_PRINT_FIFO_NAME(
            fifo_name, NA_SM_MAX_FILENAME, uri, pair_index, pair);
        NA_CHECK_SUBSYS_ERROR(ctx, rc < 0 || rc > NA_SM_MAX_FILENAME, done, ret,
            NA_OVERFLOW, "NA_SM_PRINT_FIFO_NAME() failed, rc: %d", rc);

        NA_LOG_SUBSYS_DEBUG(ctx, "unlink() %s", fifo_name);
        rc = unlink(fifo_name);
        NA_CHECK_SUBSYS_ERROR(ctx, rc == -1, done, ret,
            na_sm_errno_to_na(errno), "unlink() failed (%s)", strerror(errno));
    }
#endif

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_sm_event_set(int event)
{
    na_return_t ret = NA_SUCCESS;
#ifdef HG_UTIL_HAS_SYSEVENTFD_H
    int rc;

    rc = hg_event_set(event);
    NA_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, done, ret,
        na_sm_errno_to_na(errno), "hg_event_set() failed");
#else
    uint64_t count = 1;
    ssize_t s;

    s = write(event, &count, sizeof(uint64_t));
    NA_CHECK_SUBSYS_ERROR(ctx, s != sizeof(uint64_t), done, ret,
        na_sm_errno_to_na(errno), "write() failed (%s)", strerror(errno));
#endif

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_sm_event_get(int event, bool *signaled)
{
    na_return_t ret = NA_SUCCESS;
#ifdef HG_UTIL_HAS_SYSEVENTFD_H
    int rc;

    rc = hg_event_get(event, (bool *) signaled);
    NA_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, done, ret,
        na_sm_errno_to_na(errno), "hg_event_get() failed");
#else
    uint64_t count = 1;
    ssize_t s;

    s = read(event, &count, sizeof(uint64_t));
    if (s != sizeof(uint64_t)) {
        if (likely(errno == EAGAIN)) {
            *signaled = false;
            goto done;
        } else
            NA_GOTO_SUBSYS_ERROR(ctx, done, ret, na_sm_errno_to_na(errno),
                "read() failed (%s)", strerror(errno));
    }

    *signaled = true;
#endif

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_poll_register(hg_poll_set_t *poll_set, int fd, void *ptr)
{
    struct hg_poll_event event = {.events = HG_POLLIN, .data.ptr = ptr};
    na_return_t ret = NA_SUCCESS;
    int rc;

    rc = hg_poll_add(poll_set, fd, &event);
    NA_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, done, ret,
        na_sm_errno_to_na(errno), "hg_poll_add() failed");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_poll_deregister(hg_poll_set_t *poll_set, int fd)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    rc = hg_poll_remove(poll_set, fd);
    NA_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, done, ret,
        na_sm_errno_to_na(errno), "hg_poll_remove() failed");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_endpoint_open(struct na_sm_endpoint *na_sm_endpoint, const char *name,
    bool listen, bool no_wait, uint32_t nofile_max)
{
    static hg_atomic_int32_t sm_id_g = HG_ATOMIC_VAR_INIT(0);
    struct na_sm_addr_key addr_key = {0, 0};
    struct na_sm_region *shared_region = NULL;
    char uri[NA_SM_MAX_FILENAME], *uri_p = NULL;
    uint8_t queue_pair_idx = 0;
    bool queue_pair_reserved = false, sock_registered = false,
         tx_notify_registered = false;
    int tx_notify = -1, rx_notify = -1;
    na_return_t ret = NA_SUCCESS, err_ret;

    /* Get PID */
    addr_key.pid = getpid();

    /* Generate new SM ID (TODO fix that to avoid reaching limit) */
    addr_key.id = ((unsigned int) (hg_atomic_incr32(&sm_id_g) - 1)) & 0xff;
    NA_CHECK_SUBSYS_ERROR(fatal, addr_key.id > UINT8_MAX, error, ret,
        NA_OVERFLOW, "Reached maximum number of SM instances for this process");

    /* Save listen state */
    na_sm_endpoint->listen = listen;

    NA_LOG_SUBSYS_DEBUG(cls, "Opening new endpoint for PID=%d, ID=%u",
        addr_key.pid, addr_key.id);

    /* Initialize queues */
    STAILQ_INIT(&na_sm_endpoint->unexpected_msg_queue.queue);
    hg_thread_spin_init(&na_sm_endpoint->unexpected_msg_queue.lock);

    TAILQ_INIT(&na_sm_endpoint->unexpected_op_queue.queue);
    hg_thread_spin_init(&na_sm_endpoint->unexpected_op_queue.lock);

    TAILQ_INIT(&na_sm_endpoint->expected_op_queue.queue);
    hg_thread_spin_init(&na_sm_endpoint->expected_op_queue.lock);

    TAILQ_INIT(&na_sm_endpoint->retry_op_queue.queue);
    hg_thread_spin_init(&na_sm_endpoint->retry_op_queue.lock);

    /* Initialize number of fds */
    hg_atomic_init32(&na_sm_endpoint->nofile, 0);
    na_sm_endpoint->nofile_max = nofile_max;

    /* Initialize poll addr list */
    LIST_INIT(&na_sm_endpoint->poll_addr_list.list);
    hg_thread_spin_init(&na_sm_endpoint->poll_addr_list.lock);

    /* Create addr hash-table */
    na_sm_endpoint->addr_map.map =
        hg_hash_table_new(na_sm_addr_key_hash, na_sm_addr_key_equal);
    NA_CHECK_SUBSYS_ERROR(cls, na_sm_endpoint->addr_map.map == NULL, error, ret,
        NA_NOMEM, "hg_hash_table_new() failed");
    hg_thread_rwlock_init(&na_sm_endpoint->addr_map.lock);

    if (listen) {
        /* Create URI */
        if (name) {
            NA_LOG_SUBSYS_DEBUG(
                cls, "Using passed endpoint name as URI %s", name);

            NA_CHECK_SUBSYS_ERROR(fatal, strchr(name, '/'), error, ret,
                NA_INVALID_ARG, "Cannot use '/' in endpoint name (passed '%s')",
                name);
            strncpy(uri, name, NA_SM_MAX_FILENAME - 1);
        } else {
            int rc;

            NA_LOG_SUBSYS_DEBUG(cls,
                "No endpoint name, generating URI from PID=%d, ID=%u",
                addr_key.pid, addr_key.id);

            rc = NA_SM_PRINT_URI(uri, NA_SM_MAX_FILENAME, addr_key);
            NA_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > NA_SM_MAX_FILENAME, error,
                ret, NA_OVERFLOW, "NA_SM_PRINT_URI() failed, rc: %d", rc);
        }
        uri_p = uri;

        /* If we're listening, create a new shm region using URI */
        ret = na_sm_region_open(uri_p, true, &shared_region);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not open shared-memory region");

        /* Keep addr key in shared-region in case URI does not have PID/ID */
        shared_region->addr_key = addr_key;

        /* Reserve queue pair for loopback */
        ret = na_sm_queue_pair_reserve(shared_region, &queue_pair_idx);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not reserve queue pair");
        queue_pair_reserved = true;
    }

    if (!no_wait) {
        /* Create poll set to wait for events */
        na_sm_endpoint->poll_set = hg_poll_create();
        NA_CHECK_SUBSYS_ERROR(cls, na_sm_endpoint->poll_set == NULL, error, ret,
            na_sm_errno_to_na(errno), "Cannot create poll set");
        hg_atomic_incr32(&na_sm_endpoint->nofile);

        /* Create endpoint sock */
        ret = na_sm_sock_open(uri_p, listen, &na_sm_endpoint->sock);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not open sock");
        hg_atomic_incr32(&na_sm_endpoint->nofile);

        if (listen) {
            na_sm_endpoint->sock_poll_type = NA_SM_POLL_SOCK;
            NA_LOG_SUBSYS_DEBUG(
                cls, "Registering sock %d for polling", na_sm_endpoint->sock);
            /* Add sock to poll set (ony required if we're listening) */
            ret = na_sm_poll_register(na_sm_endpoint->poll_set,
                na_sm_endpoint->sock, &na_sm_endpoint->sock_poll_type);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, error, ret, "Could not add sock to poll set");
            sock_registered = true;
        }

        /* Create local tx signaling event */
        tx_notify = hg_event_create();
        NA_CHECK_SUBSYS_ERROR(cls, tx_notify == -1, error, ret,
            na_sm_errno_to_na(errno), "hg_event_create() failed");
        hg_atomic_incr32(&na_sm_endpoint->nofile);

        /* Create local rx signaling event */
        rx_notify = hg_event_create();
        NA_CHECK_SUBSYS_ERROR(cls, rx_notify == -1, error, ret,
            na_sm_errno_to_na(errno), "hg_event_create() failed");
        hg_atomic_incr32(&na_sm_endpoint->nofile);
    } else
        na_sm_endpoint->sock = -1;

    /* Allocate source address */
    ret = na_sm_addr_create(
        na_sm_endpoint, uri_p, &addr_key, false, &na_sm_endpoint->source_addr);
    NA_CHECK_SUBSYS_NA_ERROR(
        cls, error, ret, "Could not allocate source address");

    if (listen) {
        na_sm_endpoint->source_addr->queue_pair_idx = queue_pair_idx;
        na_sm_endpoint->source_addr->shared_region = shared_region;

        na_sm_endpoint->source_addr->tx_queue =
            &shared_region->queue_pairs[queue_pair_idx].tx_queue;
        /* Tx = Rx for loopback */
        na_sm_endpoint->source_addr->rx_queue =
            na_sm_endpoint->source_addr->tx_queue;
    }

    /* Add source tx/rx notify to poll set for local notifications */
    if (!no_wait) {
        na_sm_endpoint->source_addr->tx_notify = tx_notify;
        na_sm_endpoint->source_addr->tx_poll_type = NA_SM_POLL_TX_NOTIFY;
        NA_LOG_SUBSYS_DEBUG(
            cls, "Registering tx notify %d for polling", tx_notify);
        ret = na_sm_poll_register(na_sm_endpoint->poll_set, tx_notify,
            &na_sm_endpoint->source_addr->tx_poll_type);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not add tx notify to poll set");
        tx_notify_registered = true;

        na_sm_endpoint->source_addr->rx_notify = rx_notify;
        na_sm_endpoint->source_addr->rx_poll_type = NA_SM_POLL_RX_NOTIFY;
        NA_LOG_SUBSYS_DEBUG(
            cls, "Registering rx notify %d for polling", rx_notify);
        ret = na_sm_poll_register(na_sm_endpoint->poll_set, rx_notify,
            &na_sm_endpoint->source_addr->rx_poll_type);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not add rx notify to poll set");
    }

    hg_atomic_or32(&na_sm_endpoint->source_addr->status, NA_SM_ADDR_RESOLVED);

    if (listen) {
        /* Add address to list of addresses to poll */
        hg_thread_spin_lock(&na_sm_endpoint->poll_addr_list.lock);
        LIST_INSERT_HEAD(&na_sm_endpoint->poll_addr_list.list,
            na_sm_endpoint->source_addr, entry);
        hg_thread_spin_unlock(&na_sm_endpoint->poll_addr_list.lock);
    }

    return ret;

error:
    if (na_sm_endpoint->source_addr)
        na_sm_addr_destroy(na_sm_endpoint->source_addr);
    if (tx_notify > 0) {
        if (tx_notify_registered) {
            err_ret =
                na_sm_poll_deregister(na_sm_endpoint->poll_set, tx_notify);
            NA_CHECK_SUBSYS_ERROR_DONE(
                cls, err_ret != NA_SUCCESS, "na_sm_poll_deregister() failed");
        }
        hg_event_destroy(tx_notify);
        hg_atomic_decr32(&na_sm_endpoint->nofile);
    }
    if (rx_notify > 0) {
        hg_event_destroy(tx_notify);
        hg_atomic_decr32(&na_sm_endpoint->nofile);
    }
    if (sock_registered) {
        err_ret = na_sm_poll_deregister(
            na_sm_endpoint->poll_set, na_sm_endpoint->sock);
        NA_CHECK_SUBSYS_ERROR_DONE(
            cls, err_ret != NA_SUCCESS, "na_sm_poll_deregister() failed");
    }
    if (na_sm_endpoint->sock > 0) {
        err_ret = na_sm_sock_close(uri_p, na_sm_endpoint->sock);
        NA_CHECK_SUBSYS_ERROR_DONE(
            cls, err_ret != NA_SUCCESS, "na_sm_sock_close() failed");
        hg_atomic_decr32(&na_sm_endpoint->nofile);
    }
    if (na_sm_endpoint->poll_set) {
        hg_poll_destroy(na_sm_endpoint->poll_set);
        hg_atomic_decr32(&na_sm_endpoint->nofile);
    }
    if (queue_pair_reserved)
        na_sm_queue_pair_release(shared_region, queue_pair_idx);
    if (shared_region)
        na_sm_region_close(uri_p, shared_region);
    if (na_sm_endpoint->addr_map.map) {
        hg_hash_table_free(na_sm_endpoint->addr_map.map);
        hg_thread_rwlock_destroy(&na_sm_endpoint->addr_map.lock);
    }

    hg_thread_spin_destroy(&na_sm_endpoint->unexpected_msg_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->unexpected_op_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->expected_op_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->retry_op_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->poll_addr_list.lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_endpoint_close(struct na_sm_endpoint *na_sm_endpoint)
{
    struct na_sm_addr *source_addr = na_sm_endpoint->source_addr;
    na_return_t ret = NA_SUCCESS;
    bool empty;

    /* Check that poll addr list is empty */
    empty = LIST_EMPTY(&na_sm_endpoint->poll_addr_list.list);
    if (!empty) {
        struct na_sm_addr *na_sm_addr;

        na_sm_addr = LIST_FIRST(&na_sm_endpoint->poll_addr_list.list);
        while (na_sm_addr) {
            struct na_sm_addr *next = LIST_NEXT(na_sm_addr, entry);

            LIST_REMOVE(na_sm_addr, entry);

            /* Destroy remaining addresses */
            if (na_sm_addr != source_addr)
                na_sm_addr_destroy(na_sm_addr);
            na_sm_addr = next;
        }
        /* Sanity check */
        empty = LIST_EMPTY(&na_sm_endpoint->poll_addr_list.list);
    }
    NA_CHECK_SUBSYS_ERROR(cls, empty == false, done, ret, NA_BUSY,
        "Poll addr list should be empty");

    /* Check that unexpected message queue is empty */
    empty = STAILQ_EMPTY(&na_sm_endpoint->unexpected_msg_queue.queue);
    NA_CHECK_SUBSYS_ERROR(cls, empty == false, done, ret, NA_BUSY,
        "Unexpected msg queue should be empty");

    /* Check that unexpected op queue is empty */
    empty = TAILQ_EMPTY(&na_sm_endpoint->unexpected_op_queue.queue);
    NA_CHECK_SUBSYS_ERROR(cls, empty == false, done, ret, NA_BUSY,
        "Unexpected op queue should be empty");

    /* Check that expected op queue is empty */
    empty = TAILQ_EMPTY(&na_sm_endpoint->expected_op_queue.queue);
    NA_CHECK_SUBSYS_ERROR(cls, empty == false, done, ret, NA_BUSY,
        "Expected op queue should be empty");

    /* Check that retry op queue is empty */
    empty = TAILQ_EMPTY(&na_sm_endpoint->retry_op_queue.queue);
    NA_CHECK_SUBSYS_ERROR(cls, empty == false, done, ret, NA_BUSY,
        "Retry op queue should be empty");

    if (source_addr) {
        if (source_addr->shared_region) {
            na_sm_queue_pair_release(
                source_addr->shared_region, source_addr->queue_pair_idx);

            ret = na_sm_region_close(
                source_addr->uri, source_addr->shared_region);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, done, ret, "na_sm_region_close() failed");
            source_addr->shared_region = NULL;
        }
        if (source_addr->tx_notify > 0) {
            int rc;

            ret = na_sm_poll_deregister(
                na_sm_endpoint->poll_set, source_addr->tx_notify);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, done, ret, "na_sm_poll_deregister() failed");

            rc = hg_event_destroy(source_addr->tx_notify);
            NA_CHECK_SUBSYS_ERROR(cls, rc != HG_UTIL_SUCCESS, done, ret,
                na_sm_errno_to_na(errno), "hg_event_destroy() failed");
            hg_atomic_decr32(&na_sm_endpoint->nofile);
        }
        if (source_addr->rx_notify > 0) {
            int rc;

            ret = na_sm_poll_deregister(
                na_sm_endpoint->poll_set, source_addr->rx_notify);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, done, ret, "na_sm_poll_deregister() failed");

            rc = hg_event_destroy(source_addr->rx_notify);
            NA_CHECK_SUBSYS_ERROR(cls, rc != HG_UTIL_SUCCESS, done, ret,
                na_sm_errno_to_na(errno), "hg_event_destroy() failed");
            hg_atomic_decr32(&na_sm_endpoint->nofile);
        }
        if (na_sm_endpoint->sock > 0) {
            if (na_sm_endpoint->listen) {
                ret = na_sm_poll_deregister(
                    na_sm_endpoint->poll_set, na_sm_endpoint->sock);
                NA_CHECK_SUBSYS_NA_ERROR(
                    cls, done, ret, "na_sm_poll_deregister() failed");
            }
            ret = na_sm_sock_close(source_addr->uri, na_sm_endpoint->sock);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, done, ret, "na_sm_sock_close() failed");
            hg_atomic_decr32(&na_sm_endpoint->nofile);

            na_sm_endpoint->sock = -1;
        }
        na_sm_addr_destroy(source_addr);
        na_sm_endpoint->source_addr = NULL;
    }

    if (na_sm_endpoint->poll_set) {
        int rc = hg_poll_destroy(na_sm_endpoint->poll_set);
        NA_CHECK_SUBSYS_ERROR(cls, rc != HG_UTIL_SUCCESS, done, ret,
            na_sm_errno_to_na(errno), "hg_poll_destroy() failed");
        hg_atomic_decr32(&na_sm_endpoint->nofile);

        na_sm_endpoint->poll_set = NULL;
    }

    /* Free hash table */
    if (na_sm_endpoint->addr_map.map) {
        hg_hash_table_free(na_sm_endpoint->addr_map.map);
        hg_thread_rwlock_destroy(&na_sm_endpoint->addr_map.lock);
    }

    /* Check that all fds have been freed */
    NA_CHECK_SUBSYS_ERROR(cls, hg_atomic_get32(&na_sm_endpoint->nofile) != 0,
        done, ret, NA_BUSY,
        "File descriptors remain opened on this endpoint (nofile=%d)",
        hg_atomic_get32(&na_sm_endpoint->nofile));

    /* Destroy mutexes */
    hg_thread_spin_destroy(&na_sm_endpoint->unexpected_msg_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->unexpected_op_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->expected_op_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->retry_op_queue.lock);
    hg_thread_spin_destroy(&na_sm_endpoint->poll_addr_list.lock);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_queue_pair_reserve(struct na_sm_region *na_sm_region, uint8_t *index)
{
    unsigned int j = 0;

    do {
        int64_t bits = (int64_t) 1;
        unsigned int i = 0;

        do {
            int64_t available =
                hg_atomic_get64(&na_sm_region->available.val[j]);
            if (!available) {
                j++;
                break;
            }

            if ((available & bits) != bits) {
                /* Already reserved */
                hg_atomic_fence();
                i++;
                bits <<= 1;
                continue;
            }

            if (hg_atomic_cas64(&na_sm_region->available.val[j], available,
                    available & ~bits)) {
#ifdef NA_HAS_DEBUG
                char buf[65] = {'\0'};
                available = hg_atomic_get64(&na_sm_region->available.val[j]);
                NA_LOG_SUBSYS_DEBUG(addr,
                    "Reserved pair index %u\n### Available: %s", (i + (j * 64)),
                    lltoa((uint64_t) available, buf, 2));
#endif
                *index = (uint8_t) (i + (j * 64));
                return NA_SUCCESS;
            }

            /* Can't use atomic XOR directly, if there is a race and the cas
             * fails, we should be able to pick the next one available */
        } while (i < 64);
    } while (j < 4);

    return NA_AGAIN;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_queue_pair_release(struct na_sm_region *na_sm_region, uint8_t index)
{
    hg_atomic_or64(
        &na_sm_region->available.val[index / 64], (int64_t) 1 << index % 64);
    NA_LOG_SUBSYS_DEBUG(addr, "Released pair index %u", index);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE struct na_sm_addr *
na_sm_addr_map_lookup(
    struct na_sm_map *na_sm_map, struct na_sm_addr_key *addr_key)
{
    hg_hash_table_value_t value = NULL;

    /* Lookup key */
    hg_thread_rwlock_rdlock(&na_sm_map->lock);
    value =
        hg_hash_table_lookup(na_sm_map->map, (hg_hash_table_key_t) addr_key);
    hg_thread_rwlock_release_rdlock(&na_sm_map->lock);

    return (value == HG_HASH_TABLE_NULL) ? NULL : (struct na_sm_addr *) value;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_map_insert(struct na_sm_endpoint *na_sm_endpoint,
    struct na_sm_map *na_sm_map, const char *uri,
    struct na_sm_addr_key *addr_key, struct na_sm_addr **na_sm_addr_p)
{
    struct na_sm_addr *na_sm_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    hg_thread_rwlock_wrlock(&na_sm_map->lock);

    /* Look up again to prevent race between lock release/acquire */
    na_sm_addr = (struct na_sm_addr *) hg_hash_table_lookup(
        na_sm_map->map, (hg_hash_table_key_t) addr_key);
    if (na_sm_addr) {
        ret = NA_EXIST; /* Entry already exists */
        goto done;
    }

    /* Allocate address */
    ret = na_sm_addr_create(na_sm_endpoint, uri, addr_key, false, &na_sm_addr);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret, "Could not allocate address");

    /* Insert new value */
    rc = hg_hash_table_insert(na_sm_map->map,
        (hg_hash_table_key_t) &na_sm_addr->addr_key,
        (hg_hash_table_value_t) na_sm_addr);
    NA_CHECK_SUBSYS_ERROR(
        addr, rc == 0, error, ret, NA_NOMEM, "hg_hash_table_insert() failed");

done:
    hg_thread_rwlock_release_wrlock(&na_sm_map->lock);

    *na_sm_addr_p = na_sm_addr;

    return ret;

error:
    hg_thread_rwlock_release_wrlock(&na_sm_map->lock);
    if (na_sm_addr)
        na_sm_addr_destroy(na_sm_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_map_remove(
    struct na_sm_map *na_sm_map, struct na_sm_addr_key *addr_key)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    hg_thread_rwlock_wrlock(&na_sm_map->lock);
    if (hg_hash_table_lookup(na_sm_map->map, (hg_hash_table_key_t) addr_key) ==
        HG_HASH_TABLE_NULL)
        goto unlock;

    rc = hg_hash_table_remove(na_sm_map->map, (hg_hash_table_key_t) addr_key);
    NA_CHECK_SUBSYS_ERROR_DONE(addr, rc == 0, "Could not remove key");

unlock:
    hg_thread_rwlock_release_wrlock(&na_sm_map->lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_create(struct na_sm_endpoint *na_sm_endpoint, const char *uri,
    struct na_sm_addr_key *addr_key, bool unexpected,
    struct na_sm_addr **addr_p)
{
    struct na_sm_addr *na_sm_addr = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Allocate new addr */
    na_sm_addr = (struct na_sm_addr *) malloc(sizeof(struct na_sm_addr));
    NA_CHECK_SUBSYS_ERROR(addr, na_sm_addr == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA SM addr");
    memset(na_sm_addr, 0, sizeof(struct na_sm_addr));
    na_sm_addr->endpoint = na_sm_endpoint;
    na_sm_addr->unexpected = unexpected;
    hg_atomic_init32(&na_sm_addr->refcount, 1);
    hg_atomic_init32(&na_sm_addr->status, 0);
    hg_thread_mutex_init(&na_sm_addr->resolve_lock);

    /* Keep a copy of the URI to open SHM/sock paths */
    if (uri) {
        na_sm_addr->uri = strdup(uri);
        NA_CHECK_SUBSYS_ERROR(cls, na_sm_addr->uri == NULL, error, ret,
            NA_NOMEM, "Could not dup URI");
    }

    /* Assign PID/ID */
    na_sm_addr->addr_key = *addr_key;

    /* Default values */
    na_sm_addr->tx_notify = -1;
    na_sm_addr->rx_notify = -1;

    *addr_p = na_sm_addr;

    return NA_SUCCESS;

error:
    free(na_sm_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_addr_destroy(struct na_sm_addr *na_sm_addr)
{
    if (na_sm_addr->shared_region) {
        (void) na_sm_addr_release(na_sm_addr);
    }

    /* Only remove addresses from lookups */
    if (!na_sm_addr->unexpected &&
        na_sm_addr != na_sm_addr->endpoint->source_addr) {
        na_sm_addr_map_remove(
            &na_sm_addr->endpoint->addr_map, &na_sm_addr->addr_key);
    }

    hg_thread_mutex_destroy(&na_sm_addr->resolve_lock);
    free(na_sm_addr->uri);
    free(na_sm_addr);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_addr_ref_incr(struct na_sm_addr *na_sm_addr)
{
    hg_atomic_incr32(&na_sm_addr->refcount);
}

/*---------------------------------------------------------------------------*/
static void
na_sm_addr_ref_decr(struct na_sm_addr *na_sm_addr)
{
    struct na_sm_endpoint *na_sm_endpoint = na_sm_addr->endpoint;
    int32_t refcount = hg_atomic_decr32(&na_sm_addr->refcount);
    bool resolved = hg_atomic_get32(&na_sm_addr->status) & NA_SM_ADDR_RESOLVED;

    if (refcount > 0 && !(refcount == 1 && !resolved))
        /* Cannot free yet unless this address was not resolved */
        return;

    NA_LOG_SUBSYS_DEBUG(addr, "Freeing addr for PID=%d, ID=%d",
        na_sm_addr->addr_key.pid, na_sm_addr->addr_key.id);

    if (resolved) {
        /* Remove address from list of addresses to poll */
        hg_thread_spin_lock(&na_sm_endpoint->poll_addr_list.lock);
        LIST_REMOVE(na_sm_addr, entry);
        hg_thread_spin_unlock(&na_sm_endpoint->poll_addr_list.lock);
    }

    /* Destroy source address */
    na_sm_addr_destroy(na_sm_addr);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_resolve(struct na_sm_addr *na_sm_addr)
{
    struct na_sm_endpoint *na_sm_endpoint = na_sm_addr->endpoint;
    union na_sm_cmd_hdr cmd_hdr = {.val = 0};
    na_return_t ret;
    int rc;

    /* Nothing to do */
    if (hg_atomic_get32(&na_sm_addr->status) & NA_SM_ADDR_RESOLVED)
        return NA_SUCCESS;

    /* Open shm region */
    if (!na_sm_addr->shared_region) {
        ret = na_sm_region_open(
            na_sm_addr->uri, false, &na_sm_addr->shared_region);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, error, ret, "Could not open shared-memory region");
    }

    /* Reserve queue pair */
    if (!(hg_atomic_get32(&na_sm_addr->status) & NA_SM_ADDR_RESERVED)) {
        ret = na_sm_queue_pair_reserve(
            na_sm_addr->shared_region, &na_sm_addr->queue_pair_idx);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, error, ret, "Could not reserve queue pair");
        hg_atomic_or32(&na_sm_addr->status, NA_SM_ADDR_RESERVED);

        /* Keep tx/rx queues for convenience */
        na_sm_addr->tx_queue =
            &na_sm_addr->shared_region->queue_pairs[na_sm_addr->queue_pair_idx]
                 .tx_queue;
        na_sm_addr->rx_queue =
            &na_sm_addr->shared_region->queue_pairs[na_sm_addr->queue_pair_idx]
                 .rx_queue;
    }

    /* Fill cmd header */
    cmd_hdr = (union na_sm_cmd_hdr){.hdr.type = NA_SM_RESERVED,
        .hdr.pid = (unsigned int) na_sm_endpoint->source_addr->addr_key.pid,
        .hdr.id = na_sm_endpoint->source_addr->addr_key.id & 0xff,
        .hdr.pair_idx = na_sm_addr->queue_pair_idx & 0xff};

    NA_LOG_SUBSYS_DEBUG(addr, "Pushing cmd with %d for %d/%u/%u val=%" PRIu64,
        cmd_hdr.hdr.type, cmd_hdr.hdr.pid, cmd_hdr.hdr.id, cmd_hdr.hdr.pair_idx,
        cmd_hdr.val);

    /* Push cmd to cmd queue */
    if (!(hg_atomic_get32(&na_sm_addr->status) & NA_SM_ADDR_CMD_PUSHED)) {
        rc = na_sm_cmd_queue_push(
            &na_sm_addr->shared_region->cmd_queue, &cmd_hdr);
        NA_CHECK_SUBSYS_ERROR(
            addr, rc == false, error, ret, NA_AGAIN, "Full queue");
        hg_atomic_or32(&na_sm_addr->status, NA_SM_ADDR_CMD_PUSHED);
    }

    /* Do not create signals if not waiting */
    if (na_sm_endpoint->poll_set) {
        /* Create tx event */
        if (na_sm_addr->tx_notify < 0) {
            ret = na_sm_event_create(na_sm_addr->uri,
                na_sm_addr->queue_pair_idx, 't', &na_sm_addr->tx_notify);
            NA_CHECK_SUBSYS_NA_ERROR(
                addr, error, ret, "Could not create event");
            hg_atomic_incr32(&na_sm_endpoint->nofile);
        }

        /* Create rx event */
        if (na_sm_addr->rx_notify < 0) {
            ret = na_sm_event_create(na_sm_addr->uri,
                na_sm_addr->queue_pair_idx, 'r', &na_sm_addr->rx_notify);
            NA_CHECK_SUBSYS_NA_ERROR(
                addr, error, ret, "Could not create event");
            hg_atomic_incr32(&na_sm_endpoint->nofile);

            na_sm_addr->rx_poll_type = NA_SM_POLL_RX_NOTIFY;
            NA_LOG_SUBSYS_DEBUG(addr, "Registering rx notify %d for polling",
                na_sm_addr->rx_notify);

            /* Add remote rx notify to poll set */
            ret = na_sm_poll_register(na_sm_endpoint->poll_set,
                na_sm_addr->rx_notify, &na_sm_addr->rx_poll_type);
            NA_CHECK_SUBSYS_NA_ERROR(
                addr, error, ret, "Could not add rx notify to poll set");
        }

        /* Send events to remote process */
        ret = na_sm_addr_event_send(na_sm_endpoint->sock, na_sm_addr->uri,
            cmd_hdr, na_sm_addr->tx_notify, na_sm_addr->rx_notify, false);
        if (unlikely(ret == NA_AGAIN))
            return ret;
        else
            NA_CHECK_SUBSYS_NA_ERROR(
                addr, error, ret, "Could not send addr events");
    }

    hg_atomic_or32(&na_sm_addr->status, NA_SM_ADDR_RESOLVED);

    /* Add address to list of addresses to poll */
    hg_thread_spin_lock(&na_sm_endpoint->poll_addr_list.lock);
    LIST_INSERT_HEAD(&na_sm_endpoint->poll_addr_list.list, na_sm_addr, entry);
    hg_thread_spin_unlock(&na_sm_endpoint->poll_addr_list.lock);

    return NA_SUCCESS;

error:
    if (na_sm_addr->shared_region) {
        na_return_t err_ret;

        if (hg_atomic_get32(&na_sm_addr->status) & NA_SM_ADDR_RESERVED) {
            na_sm_queue_pair_release(
                na_sm_addr->shared_region, na_sm_addr->queue_pair_idx);
            hg_atomic_and32(&na_sm_addr->status, ~NA_SM_ADDR_RESERVED);

            if (na_sm_addr->tx_notify > 0) {
                err_ret = na_sm_event_destroy(na_sm_addr->uri,
                    na_sm_addr->queue_pair_idx, 't', true,
                    na_sm_addr->tx_notify);
                NA_CHECK_SUBSYS_ERROR_DONE(addr, err_ret != NA_SUCCESS,
                    "na_sm_event_destroy() failed");
                hg_atomic_decr32(&na_sm_endpoint->nofile);
                na_sm_addr->tx_notify = -1;
            }
            if (na_sm_addr->rx_notify > 0) {
                err_ret = na_sm_event_destroy(na_sm_addr->uri,
                    na_sm_addr->queue_pair_idx, 'r', true,
                    na_sm_addr->rx_notify);
                NA_CHECK_SUBSYS_ERROR_DONE(addr, err_ret != NA_SUCCESS,
                    "na_sm_event_destroy() failed");
                hg_atomic_decr32(&na_sm_endpoint->nofile);
                na_sm_addr->rx_notify = -1;
            }
        }

        err_ret = na_sm_region_close(NULL, na_sm_addr->shared_region);
        NA_CHECK_SUBSYS_ERROR_DONE(addr, err_ret != NA_SUCCESS,
            "Could not close shared-memory region");
        na_sm_addr->shared_region = NULL;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_release(struct na_sm_addr *na_sm_addr)
{
    struct na_sm_endpoint *na_sm_endpoint = na_sm_addr->endpoint;
    na_return_t ret = NA_SUCCESS;

    if (na_sm_addr->unexpected) {
        /* Release queue pair */
        na_sm_queue_pair_release(
            na_sm_addr->shared_region, na_sm_addr->queue_pair_idx);
    } else {
        union na_sm_cmd_hdr cmd_hdr = {.val = 0};

        /* Fill cmd header */
        cmd_hdr = (union na_sm_cmd_hdr){.hdr.type = NA_SM_RELEASED,
            .hdr.pid = (unsigned int) na_sm_endpoint->source_addr->addr_key.pid,
            .hdr.id = na_sm_endpoint->source_addr->addr_key.id & 0xff,
            .hdr.pair_idx = na_sm_addr->queue_pair_idx & 0xff};

        if (na_sm_endpoint->poll_set) {
            /* Send events to remote process (silence error as this is best
             * effort to clean up resources) */
            ret = na_sm_addr_event_send(
                na_sm_endpoint->sock, na_sm_addr->uri, cmd_hdr, -1, -1, true);
            NA_CHECK_SUBSYS_NA_ERROR(
                addr, done, ret, "Could not send addr events");
        } else {
            bool rc;

            NA_LOG_SUBSYS_DEBUG(addr,
                "Pushing cmd with %d for %d/%u/%u val=%" PRIu64,
                cmd_hdr.hdr.type, cmd_hdr.hdr.pid, cmd_hdr.hdr.id,
                cmd_hdr.hdr.pair_idx, cmd_hdr.val);

            /* Push cmd to cmd queue */
            rc = na_sm_cmd_queue_push(
                &na_sm_addr->shared_region->cmd_queue, &cmd_hdr);
            NA_CHECK_SUBSYS_ERROR(
                addr, rc == false, done, ret, NA_AGAIN, "Full queue");
        }

        /* Close shared-memory region */
        ret = na_sm_region_close(NULL, na_sm_addr->shared_region);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, done, ret, "Could not close shared-memory region");
    }

    if (na_sm_addr->tx_notify > 0) {
        ret = na_sm_event_destroy(na_sm_addr->uri, na_sm_addr->queue_pair_idx,
            't', !na_sm_addr->unexpected, na_sm_addr->tx_notify);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, done, ret, "na_sm_event_destroy() failed");
        hg_atomic_decr32(&na_sm_endpoint->nofile);
    }

    if (na_sm_addr->rx_notify > 0) {
        ret = na_sm_poll_deregister(
            na_sm_endpoint->poll_set, na_sm_addr->rx_notify);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, done, ret, "na_sm_poll_deregister() failed");

        ret = na_sm_event_destroy(na_sm_addr->uri, na_sm_addr->queue_pair_idx,
            'r', !na_sm_addr->unexpected, na_sm_addr->rx_notify);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, done, ret, "na_sm_event_destroy() failed");
        hg_atomic_decr32(&na_sm_endpoint->nofile);
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_event_send(int sock, const char *dest_name,
    union na_sm_cmd_hdr cmd_hdr, int tx_notify, int rx_notify,
    bool ignore_error)
{
    struct sockaddr_un addr;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    /* Contains the file descriptors to pass */
    int fds[2] = {tx_notify, rx_notify};
    union {
        /* ancillary data buffer, wrapped in a union in order to ensure
           it is suitably aligned */
        char buf[CMSG_SPACE(sizeof(fds))];
        struct cmsghdr align;
    } u;
    int *fdptr;
    struct iovec iovec[1];
    ssize_t nsend;
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* Generate named socket path */
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    rc = NA_SM_PRINT_SOCK_PATH(addr.sun_path, NA_SM_MAX_FILENAME, dest_name);
    NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > NA_SM_MAX_FILENAME, done, ret,
        NA_OVERFLOW, "NA_SM_PRINT_SOCK_PATH() failed, rc: %d", rc);
    strcat(addr.sun_path, NA_SM_SOCK_NAME);

    /* Set address of destination */
    msg.msg_name = &addr;
    msg.msg_namelen = (socklen_t) SUN_LEN(&addr);
    msg.msg_flags = 0; /* unused */

    /* Send cmd */
    iovec[0].iov_base = &cmd_hdr;
    iovec[0].iov_len = sizeof(cmd_hdr);
    msg.msg_iov = iovec;
    msg.msg_iovlen = 1;

    if (tx_notify > 0 && rx_notify > 0) {
        /* Send notify event descriptors as ancillary data */
        msg.msg_control = u.buf;
        msg.msg_controllen = sizeof(u.buf);
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(fds));

        /* Initialize the payload */
        fdptr = (int *) CMSG_DATA(cmsg);
        memcpy(fdptr, fds, sizeof(fds));
    } else {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
    }

    nsend = sendmsg(sock, &msg, 0);
    if (!ignore_error) {
        if (unlikely(nsend == -1 && errno == ETOOMANYREFS))
            ret = NA_AGAIN;
        else
            NA_CHECK_SUBSYS_ERROR(addr, nsend == -1, done, ret,
                na_sm_errno_to_na(errno), "sendmsg() failed (%s)",
                strerror(errno));
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_event_recv(int sock, union na_sm_cmd_hdr *cmd_hdr, int *tx_notify,
    int *rx_notify, bool *received)
{
    struct msghdr msg;
    struct cmsghdr *cmsg;
    int *fdptr;
    int fds[2];
    union {
        /* ancillary data buffer, wrapped in a union in order to ensure
           it is suitably aligned */
        char buf[CMSG_SPACE(sizeof(fds))];
        struct cmsghdr align;
    } u;
    ssize_t nrecv;
    struct iovec iovec[1];
    na_return_t ret = NA_SUCCESS;

    /* Ignore address of source */
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_flags = 0; /* unused */

    /* Recv reserved queue pair index */
    iovec[0].iov_base = cmd_hdr;
    iovec[0].iov_len = sizeof(*cmd_hdr);
    msg.msg_iov = iovec;
    msg.msg_iovlen = 1;

    /* Recv notify event descriptor as ancillary data */
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof u.buf;

    nrecv = recvmsg(sock, &msg, 0);
    if (nrecv == -1) {
        if (likely(errno == EAGAIN)) {
            *received = false;
            goto done;
        } else
            NA_GOTO_SUBSYS_ERROR(addr, done, ret, na_sm_errno_to_na(errno),
                "recvmsg() failed (%s)", strerror(errno));
    }

    *received = true;

    /* Retrieve ancillary data */
    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg) {
        fdptr = (int *) CMSG_DATA(cmsg);
        memcpy(fds, fdptr, sizeof(fds));

        *tx_notify = fds[0];
        *rx_notify = fds[1];
    } else {
        *tx_notify = -1;
        *rx_notify = -1;
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_send(struct na_sm_class *na_sm_class, na_context_t *context,
    na_cb_type_t cb_type, na_cb_t callback, void *arg, const void *buf,
    size_t buf_size, struct na_sm_addr *na_sm_addr, na_tag_t tag,
    struct na_sm_op_id *na_sm_op_id)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(msg, buf_size > NA_SM_COPY_BUF_SIZE, error, ret,
        NA_OVERFLOW, "Exceeds copy buf size, %zu", buf_size);

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_sm_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_sm_op_id->completion_data.callback_info.type));

    NA_SM_OP_RESET(na_sm_op_id, context, cb_type, callback, arg, na_sm_addr);

    /* TODO we assume that buf remains valid (safe because we pre-allocate
     * buffers) */
    na_sm_op_id->info.msg = (struct na_sm_msg_info){
        .buf.const_ptr = buf, .buf_size = buf_size, .tag = tag};

    ret = na_sm_msg_send_post(
        &na_sm_class->endpoint, cb_type, buf, buf_size, na_sm_addr, tag);
    if (ret == NA_SUCCESS) {
        /* Immediate completion, add directly to completion queue. */
        na_sm_complete(na_sm_op_id, NA_SUCCESS);

        /* Notify local completion */
        na_sm_complete_signal(na_sm_class);
    } else if (ret == NA_AGAIN) {
        na_sm_op_retry(na_sm_class, na_sm_op_id);
        return NA_SUCCESS;
    } else
        NA_CHECK_SUBSYS_NA_ERROR(msg, release, ret, "Could not post msg");

    return NA_SUCCESS;

release:
    NA_SM_OP_RELEASE(na_sm_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_send_post(struct na_sm_endpoint *na_sm_endpoint, na_cb_type_t cb_type,
    const void *buf, size_t buf_size, struct na_sm_addr *na_sm_addr,
    na_tag_t tag)
{
    unsigned int buf_idx = 0;
    union na_sm_msg_hdr msg_hdr;
    na_return_t ret;
    bool rc;

    /* Attempt to resolve address first if not resolved */
    if (hg_atomic_get32(&na_sm_addr->status) != NA_SM_ADDR_RESOLVED) {
        hg_thread_mutex_lock(&na_sm_addr->resolve_lock);
        ret = na_sm_addr_resolve(na_sm_addr);
        hg_thread_mutex_unlock(&na_sm_addr->resolve_lock);
        if (unlikely(ret == NA_AGAIN))
            return NA_AGAIN;
        else
            NA_CHECK_SUBSYS_NA_ERROR(
                addr, error, ret, "Could not resolve address");
    }

    /* No need to reserve for 0-size messages */
    if (buf_size > 0) {
        /* Try to reserve buffer atomically */
        ret =
            na_sm_buf_reserve(&na_sm_addr->shared_region->copy_bufs, &buf_idx);
        if (unlikely(ret == NA_AGAIN))
            return NA_AGAIN;

        /* Reservation succeeded, copy buffer */
        na_sm_buf_copy_to(
            &na_sm_addr->shared_region->copy_bufs, buf_idx, buf, buf_size);
    }

    /* Post message to queue */
    msg_hdr = (union na_sm_msg_hdr){.hdr.type = cb_type,
        .hdr.buf_idx = buf_idx & 0xff,
        .hdr.buf_size = buf_size & 0xffff,
        .hdr.tag = tag};

    rc = na_sm_msg_queue_push(na_sm_addr->tx_queue, &msg_hdr);
    NA_CHECK_SUBSYS_ERROR(
        msg, rc == false, release, ret, NA_AGAIN, "Full queue");

    /* Notify remote if notifications are enabled */
    if (na_sm_addr == na_sm_endpoint->source_addr &&
        na_sm_addr->rx_notify > 0) {
        int rc1 = hg_event_set(na_sm_addr->rx_notify);
        NA_CHECK_SUBSYS_ERROR(msg, rc1 != HG_UTIL_SUCCESS, release, ret,
            na_sm_errno_to_na(errno), "Could not send completion notification");
    } else if (na_sm_addr->tx_notify > 0) {
        ret = na_sm_event_set(na_sm_addr->tx_notify);
        NA_CHECK_SUBSYS_NA_ERROR(
            msg, release, ret, "Could not send completion notification");
    }

    return NA_SUCCESS;

release:
    if (buf_size > 0)
        na_sm_buf_release(&na_sm_addr->shared_region->copy_bufs, buf_idx);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_sm_buf_reserve(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int *index)
{
    int64_t bits = (int64_t) 1;
    unsigned int i = 0;

    do {
        int64_t available = hg_atomic_get64(&na_sm_copy_buf->available.val);
        if (!available) {
            /* Nothing available */
            break;
        }
        if ((available & bits) != bits) {
            /* Already reserved */
            hg_atomic_fence();
            i++;
            bits <<= 1;
            continue;
        }

        if (hg_atomic_cas64(
                &na_sm_copy_buf->available.val, available, available & ~bits)) {
#ifdef NA_HAS_DEBUG
            char buf[65] = {'\0'};
            available = hg_atomic_get64(&na_sm_copy_buf->available.val);
            NA_LOG_SUBSYS_DEBUG(msg, "Reserved bit index %u\n### Available: %s",
                i, lltoa((uint64_t) available, buf, 2));
#endif
            *index = i;
            return NA_SUCCESS;
        }
        /* Can't use atomic XOR directly, if there is a race and the cas
         * fails, we should be able to pick the next one available */
    } while (i < NA_SM_NUM_BUFS);

    return NA_AGAIN;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_buf_release(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int index)
{
    hg_atomic_or64(&na_sm_copy_buf->available.val, (int64_t) 1 << index);
    NA_LOG_SUBSYS_DEBUG(msg, "Released bit index %u", index);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_buf_copy_to(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int index,
    const void *src, size_t n)
{
    hg_thread_spin_lock(&na_sm_copy_buf->buf_locks[index]);
    memcpy(na_sm_copy_buf->buf[index], src, n);
    hg_thread_spin_unlock(&na_sm_copy_buf->buf_locks[index]);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_buf_copy_from(struct na_sm_copy_buf *na_sm_copy_buf, unsigned int index,
    void *dest, size_t n)
{
    hg_thread_spin_lock(&na_sm_copy_buf->buf_locks[index]);
    memcpy(dest, na_sm_copy_buf->buf[index], n);
    hg_thread_spin_unlock(&na_sm_copy_buf->buf_locks[index]);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_rma(struct na_sm_class *na_sm_class, na_context_t *context,
    na_cb_type_t cb_type, na_cb_t callback, void *arg,
    na_sm_process_vm_op_t process_vm_op,
    struct na_sm_mem_handle *na_sm_mem_handle_local, na_offset_t local_offset,
    struct na_sm_mem_handle *na_sm_mem_handle_remote, na_offset_t remote_offset,
    size_t length, struct na_sm_addr *na_sm_addr,
    struct na_sm_op_id *na_sm_op_id)
{
    struct iovec *local_iov = NA_SM_IOV(na_sm_mem_handle_local),
                 *remote_iov = NA_SM_IOV(na_sm_mem_handle_remote);
    unsigned long local_iovcnt = na_sm_mem_handle_local->info.iovcnt,
                  remote_iovcnt = na_sm_mem_handle_remote->info.iovcnt;
    unsigned long local_iov_start_index = 0, remote_iov_start_index = 0;
    na_offset_t local_iov_start_offset = 0, remote_iov_start_offset = 0;
    union na_sm_iov local_trans_iov, remote_trans_iov;
    struct iovec *liov, *riov;
    unsigned long liovcnt = 0, riovcnt = 0;
    na_return_t ret;

#if !defined(NA_SM_HAS_CMA) && !defined(__APPLE__)
    NA_GOTO_SUBSYS_ERROR(
        error, ret, NA_OPNOTSUPPORTED, "Not implemented for this platform");
#endif

    switch (na_sm_mem_handle_remote->info.flags) {
        case NA_MEM_READ_ONLY:
            NA_CHECK_SUBSYS_ERROR(rma, cb_type == NA_CB_PUT, error, ret,
                NA_PERMISSION, "Registered memory requires write permission");
            break;
        case NA_MEM_WRITE_ONLY:
            NA_CHECK_SUBSYS_ERROR(rma, cb_type == NA_CB_GET, error, ret,
                NA_PERMISSION, "Registered memory requires write permission");
            break;
        case NA_MEM_READWRITE:
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(
                rma, error, ret, NA_INVALID_ARG, "Invalid memory access flag");
    }

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_sm_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_sm_op_id->completion_data.callback_info.type));

    NA_SM_OP_RESET(na_sm_op_id, context, cb_type, callback, arg, na_sm_addr);

    /* Translate local offset */
    if (local_offset > 0)
        na_sm_iov_get_index_offset(local_iov, local_iovcnt, local_offset,
            &local_iov_start_index, &local_iov_start_offset);

    if (length != na_sm_mem_handle_local->info.len) {
        liovcnt = na_sm_iov_get_count(local_iov, local_iovcnt,
            local_iov_start_index, local_iov_start_offset, length);

        if (liovcnt > NA_SM_IOV_STATIC_MAX) {
            local_trans_iov.d =
                (struct iovec *) malloc(liovcnt * sizeof(struct iovec));
            NA_CHECK_SUBSYS_ERROR(rma, local_trans_iov.d == NULL, release, ret,
                NA_NOMEM, "Could not allocate iovec");

            liov = local_trans_iov.d;
        } else
            liov = local_trans_iov.s;

        na_sm_iov_translate(local_iov, local_iovcnt, local_iov_start_index,
            local_iov_start_offset, length, liov, liovcnt);
    } else {
        liov = local_iov;
        liovcnt = local_iovcnt;
    }

    /* Translate remote offset */
    if (remote_offset > 0)
        na_sm_iov_get_index_offset(remote_iov, remote_iovcnt, remote_offset,
            &remote_iov_start_index, &remote_iov_start_offset);

    if (length != na_sm_mem_handle_remote->info.len) {
        riovcnt = na_sm_iov_get_count(remote_iov, remote_iovcnt,
            remote_iov_start_index, remote_iov_start_offset, length);

        if (riovcnt > NA_SM_IOV_STATIC_MAX) {
            remote_trans_iov.d =
                (struct iovec *) malloc(riovcnt * sizeof(struct iovec));
            NA_CHECK_SUBSYS_ERROR(rma, remote_trans_iov.d == NULL, release, ret,
                NA_NOMEM, "Could not allocate iovec");

            riov = remote_trans_iov.d;
        } else
            riov = remote_trans_iov.s;

        na_sm_iov_translate(remote_iov, remote_iovcnt, remote_iov_start_index,
            remote_iov_start_offset, length, riov, riovcnt);
    } else {
        riov = remote_iov;
        riovcnt = remote_iovcnt;
    }

    NA_LOG_SUBSYS_DEBUG(rma, "Posting rma op (op id=%p)", (void *) na_sm_op_id);

    /* NB. addr does not need to be fully "resolved" to issue RMA */
    ret = process_vm_op(
        na_sm_addr->addr_key.pid, liov, liovcnt, riov, riovcnt, length);
    NA_CHECK_SUBSYS_NA_ERROR(rma, release, ret, "process_vm_op() failed");

    /* Free before adding to completion queue */
    if (liovcnt > NA_SM_IOV_STATIC_MAX &&
        (length != na_sm_mem_handle_local->info.len))
        free(local_trans_iov.d);
    if (riovcnt > NA_SM_IOV_STATIC_MAX &&
        (length != na_sm_mem_handle_remote->info.len))
        free(remote_trans_iov.d);

    /* Immediate completion */
    na_sm_complete(na_sm_op_id, NA_SUCCESS);

    /* Notify local completion */
    na_sm_complete_signal(na_sm_class);

    return NA_SUCCESS;

release:
    if (liovcnt > NA_SM_IOV_STATIC_MAX &&
        (length != na_sm_mem_handle_local->info.len))
        free(local_trans_iov.d);
    if (riovcnt > NA_SM_IOV_STATIC_MAX &&
        (length != na_sm_mem_handle_remote->info.len))
        free(remote_trans_iov.d);

    NA_SM_OP_RELEASE(na_sm_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_iov_get_index_offset(const struct iovec *iov, unsigned long iovcnt,
    na_offset_t offset, unsigned long *iov_start_index,
    na_offset_t *iov_start_offset)
{
    na_offset_t new_iov_offset = offset, next_offset = 0;
    unsigned long i, new_iov_start_index = 0;

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
static NA_INLINE unsigned long
na_sm_iov_get_count(const struct iovec *iov, unsigned long iovcnt,
    unsigned long iov_start_index, na_offset_t iov_start_offset, size_t len)
{
    size_t remaining_len =
        len - MIN(len, iov[iov_start_index].iov_len - iov_start_offset);
    unsigned long i, iov_index;

    for (i = 1, iov_index = iov_start_index + 1;
         remaining_len > 0 && iov_index < iovcnt; i++, iov_index++) {
        /* Decrease remaining len from the len of data */
        remaining_len -= MIN(remaining_len, iov[iov_index].iov_len);
    }

    return i;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_iov_translate(const struct iovec *iov, unsigned long iovcnt,
    unsigned long iov_start_index, na_offset_t iov_start_offset, size_t len,
    struct iovec *new_iov, unsigned long new_iovcnt)
{
    size_t remaining_len = len;
    unsigned long i, iov_index;

    /* Offset is only within first segment */
    new_iov[0].iov_base =
        (char *) iov[iov_start_index].iov_base + iov_start_offset;
    new_iov[0].iov_len =
        MIN(remaining_len, iov[iov_start_index].iov_len - iov_start_offset);
    remaining_len -= new_iov[0].iov_len;

    for (i = 1, iov_index = iov_start_index + 1;
         remaining_len > 0 && i < new_iovcnt && iov_index < iovcnt;
         i++, iov_index++) {
        new_iov[i].iov_base = iov[iov_index].iov_base;
        new_iov[i].iov_len = MIN(remaining_len, iov[iov_index].iov_len);

        /* Decrease remaining len from the len of data */
        remaining_len -= new_iov[i].iov_len;
    }
}

#ifdef NA_SM_HAS_CMA
/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_process_vm_writev(pid_t pid, const struct iovec *local_iov,
    unsigned long liovcnt, const struct iovec *remote_iov,
    unsigned long riovcnt, size_t length)
{
    na_return_t ret;
    ssize_t nwrite;

    nwrite = process_vm_writev(pid, local_iov, liovcnt, remote_iov, riovcnt, 0);
    if (unlikely(nwrite < 0)) {
        if ((errno == EPERM) && na_sm_get_ptrace_scope_value()) {
            NA_GOTO_SUBSYS_ERROR(fatal, error, ret, na_sm_errno_to_na(errno),
                "process_vm_writev() failed (%s):\n"
                "Kernel Yama configuration does not allow cross-memory attach, "
                "either run as root: \n"
                "# /usr/sbin/sysctl kernel.yama.ptrace_scope=0\n"
                "or if set to restricted, add the following call to your "
                "application:\n"
                "prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);\n"
                "See https://www.kernel.org/doc/Documentation/security/Yama.txt"
                " for more details.",
                strerror(errno));
        } else
            NA_GOTO_SUBSYS_ERROR(rma, error, ret, na_sm_errno_to_na(errno),
                "process_vm_writev() failed (%s)", strerror(errno));
    }

    NA_CHECK_SUBSYS_ERROR(rma, (size_t) nwrite != length, error, ret,
        NA_MSGSIZE, "Wrote %zd bytes, was expecting %zu bytes", nwrite, length);

    return NA_SUCCESS;

error:
    return ret;
}

#elif defined(__APPLE__)
/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_process_vm_writev(pid_t pid, const struct iovec *local_iov,
    unsigned long liovcnt, const struct iovec *remote_iov,
    unsigned long riovcnt, size_t length)
{
    kern_return_t kret;
    mach_port_name_t remote_task;
    na_return_t ret;

    kret = task_for_pid(mach_task_self(), pid, &remote_task);
    NA_CHECK_SUBSYS_ERROR(fatal, kret != KERN_SUCCESS, error, ret,
        NA_PERMISSION,
        "task_for_pid() failed (%s)\n"
        "Permission must be set to access remote memory, please refer to the "
        "documentation for instructions.",
        mach_error_string(kret));
    NA_CHECK_SUBSYS_ERROR(fatal, liovcnt > 1 || riovcnt > 1, error, ret,
        NA_OPNOTSUPPORTED, "Non-contiguous transfers are not supported");

    kret =
        mach_vm_write(remote_task, (mach_vm_address_t) remote_iov[0].iov_base,
            (mach_vm_address_t) local_iov[0].iov_base,
            (mach_msg_type_number_t) length);
    NA_CHECK_SUBSYS_ERROR(rma, kret != KERN_SUCCESS, error, ret,
        NA_PROTOCOL_ERROR, "mach_vm_write() failed (%s)",
        mach_error_string(kret));

    return NA_SUCCESS;

error:
    return ret;
}
#endif

#ifdef NA_SM_HAS_CMA
/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_process_vm_readv(pid_t pid, const struct iovec *local_iov,
    unsigned long liovcnt, const struct iovec *remote_iov,
    unsigned long riovcnt, size_t length)
{
    na_return_t ret;
    ssize_t nread;

    nread = process_vm_readv(pid, local_iov, liovcnt, remote_iov, riovcnt, 0);
    if (unlikely(nread < 0)) {
        if ((errno == EPERM) && na_sm_get_ptrace_scope_value()) {
            NA_GOTO_SUBSYS_ERROR(fatal, error, ret, na_sm_errno_to_na(errno),
                "process_vm_readv() failed (%s):\n"
                "Kernel Yama configuration does not allow cross-memory attach, "
                "either run as root: \n"
                "# /usr/sbin/sysctl kernel.yama.ptrace_scope=0\n"
                "or if set to restricted, add the following call to your "
                "application:\n"
                "prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);\n"
                "See https://www.kernel.org/doc/Documentation/security/Yama.txt"
                " for more details.",
                strerror(errno));
        } else
            NA_GOTO_SUBSYS_ERROR(rma, error, ret, na_sm_errno_to_na(errno),
                "process_vm_readv() failed (%s)", strerror(errno));
    }

    NA_CHECK_SUBSYS_ERROR(rma, (size_t) nread != length, error, ret, NA_MSGSIZE,
        "Read %zd bytes, was expecting %zu bytes", nread, length);

    return NA_SUCCESS;

error:
    return ret;
}

#elif defined(__APPLE__)
/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_process_vm_readv(pid_t pid, const struct iovec *local_iov,
    unsigned long liovcnt, const struct iovec *remote_iov,
    unsigned long riovcnt, size_t length)
{
    kern_return_t kret;
    mach_port_name_t remote_task;
    mach_vm_size_t nread;
    na_return_t ret;

    kret = task_for_pid(mach_task_self(), pid, &remote_task);
    NA_CHECK_SUBSYS_ERROR(fatal, kret != KERN_SUCCESS, error, ret,
        NA_PERMISSION,
        "task_for_pid() failed (%s)\n"
        "Permission must be set to access remote memory, please refer to the "
        "documentation for instructions.",
        mach_error_string(kret));
    NA_CHECK_SUBSYS_ERROR(fatal, liovcnt > 1 || riovcnt > 1, error, ret,
        NA_OPNOTSUPPORTED, "Non-contiguous transfers are not supported");

    kret = mach_vm_read_overwrite(remote_task,
        (mach_vm_address_t) remote_iov[0].iov_base, length,
        (mach_vm_address_t) local_iov[0].iov_base, &nread);
    NA_CHECK_SUBSYS_ERROR(rma, kret != KERN_SUCCESS, error, ret,
        NA_PROTOCOL_ERROR, "mach_vm_read_overwrite() failed (%s)",
        mach_error_string(kret));

    NA_CHECK_SUBSYS_ERROR(rma, (size_t) nread != length, error, ret, NA_MSGSIZE,
        "Read %" PRIu64 " bytes, was expecting %zu bytes", nread, length);

    return NA_SUCCESS;

error:
    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress_wait(na_context_t *context,
    struct na_sm_endpoint *na_sm_endpoint, unsigned int timeout,
    unsigned int *count_p)
{
    struct hg_poll_event *events = NA_SM_CONTEXT(context)->events;
    unsigned int nevents = 0, count = 0, i;
    na_return_t ret;
    int rc;

    /* Just wait on a single event, anything greater may increase
     * latency, and slow down progress, we will not wait next round
     * if something is still in the queues */
    rc = hg_poll_wait(
        na_sm_endpoint->poll_set, timeout, NA_SM_MAX_EVENTS, events, &nevents);
    NA_CHECK_SUBSYS_ERROR(poll, rc != HG_UTIL_SUCCESS, error, ret,
        na_sm_errno_to_na(errno), "hg_poll_wait() failed");

    if (nevents == 1 && (events[0].events & HG_POLLINTR)) {
        NA_LOG_SUBSYS_DEBUG(poll_loop, "Interrupted");
        *count_p = count;
        return NA_SUCCESS;
    }

    /* Process events */
    for (i = 0; i < nevents; i++) {
        struct na_sm_addr *poll_addr = NULL;
        bool progressed_notify = false;
        bool progressed_rx = false;

        switch (*(enum na_sm_poll_type *) events[i].data.ptr) {
            case NA_SM_POLL_SOCK:
                NA_LOG_SUBSYS_DEBUG(poll_loop, "NA_SM_POLL_SOCK event");
                ret = na_sm_progress_sock(na_sm_endpoint, &progressed_notify);
                NA_CHECK_SUBSYS_NA_ERROR(
                    poll, error, ret, "Could not progress sock");
                break;
            case NA_SM_POLL_TX_NOTIFY:
                NA_LOG_SUBSYS_DEBUG(poll_loop, "NA_SM_POLL_TX_NOTIFY event");
                poll_addr = container_of(
                    events[i].data.ptr, struct na_sm_addr, tx_poll_type);
                ret = na_sm_progress_tx_notify(poll_addr, &progressed_notify);
                NA_CHECK_SUBSYS_NA_ERROR(
                    poll, error, ret, "Could not progress tx notify");
                break;
            case NA_SM_POLL_RX_NOTIFY:
                NA_LOG_SUBSYS_DEBUG(poll_loop, "NA_SM_POLL_RX_NOTIFY event");
                poll_addr = container_of(
                    events[i].data.ptr, struct na_sm_addr, rx_poll_type);

                ret = na_sm_progress_rx_notify(poll_addr, &progressed_notify);
                NA_CHECK_SUBSYS_NA_ERROR(
                    poll, error, ret, "Could not progress rx notify");

                ret = na_sm_progress_rx_queue(
                    na_sm_endpoint, poll_addr, &progressed_rx);
                NA_CHECK_SUBSYS_NA_ERROR(
                    poll, error, ret, "Could not progress rx queue");

                break;
            default:
                NA_GOTO_SUBSYS_ERROR(poll, error, ret, NA_INVALID_ARG,
                    "Operation type %d not supported",
                    *(enum na_sm_poll_type *) events[i].data.ptr);
        }
        count += (unsigned int) (progressed_rx | progressed_notify);
    }

    *count_p = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress(struct na_sm_endpoint *na_sm_endpoint, unsigned int *count_p)
{
    struct na_sm_addr_list *poll_addr_list = &na_sm_endpoint->poll_addr_list;
    struct na_sm_addr *poll_addr;
    unsigned int count = 0;
    na_return_t ret = NA_SUCCESS;

    /* Check whether something is in one of the rx queues */
    hg_thread_spin_lock(&poll_addr_list->lock);
    LIST_FOREACH (poll_addr, &poll_addr_list->list, entry) {
        bool progressed_rx = false;

        hg_thread_spin_unlock(&poll_addr_list->lock);

        ret =
            na_sm_progress_rx_queue(na_sm_endpoint, poll_addr, &progressed_rx);
        NA_CHECK_SUBSYS_NA_ERROR(
            poll, done, ret, "Could not progress rx queue");
        count += (unsigned int) progressed_rx;

        hg_thread_spin_lock(&poll_addr_list->lock);
    }
    hg_thread_spin_unlock(&poll_addr_list->lock);

    /* Look for message in cmd queue (if listening) */
    if (na_sm_endpoint->source_addr->shared_region) {
        bool progressed_cmd = false;

        ret = na_sm_progress_cmd_queue(na_sm_endpoint, &progressed_cmd);
        NA_CHECK_SUBSYS_NA_ERROR(
            poll, done, ret, "Could not progress cmd queue");
        count += (unsigned int) progressed_cmd;
    }

    *count_p = count;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress_sock(struct na_sm_endpoint *na_sm_endpoint, bool *progressed)
{
    union na_sm_cmd_hdr cmd_hdr = {.val = 0};
    int tx_notify = -1, rx_notify = -1;
    na_return_t ret = NA_SUCCESS;

    /* Attempt to receive addr info (events, queue index) */
    ret = na_sm_addr_event_recv(
        na_sm_endpoint->sock, &cmd_hdr, &tx_notify, &rx_notify, progressed);
    NA_CHECK_SUBSYS_NA_ERROR(addr, done, ret, "Could not recv addr events");

    if (*progressed) {
        if (tx_notify > 0)
            hg_atomic_incr32(&na_sm_endpoint->nofile);

        if (rx_notify > 0)
            hg_atomic_incr32(&na_sm_endpoint->nofile);

        /* Process received cmd, TODO would be nice to use cmd queue */
        ret = na_sm_process_cmd(na_sm_endpoint, cmd_hdr, tx_notify, rx_notify);
        NA_CHECK_SUBSYS_NA_ERROR(addr, done, ret, "Could not process cmd");
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress_cmd_queue(
    struct na_sm_endpoint *na_sm_endpoint, bool *progressed)
{
    union na_sm_cmd_hdr cmd_hdr = {.val = 0};
    na_return_t ret = NA_SUCCESS;

    /* Look for message in cmd queue */
    if (!na_sm_cmd_queue_pop(
            &na_sm_endpoint->source_addr->shared_region->cmd_queue, &cmd_hdr)) {
        *progressed = false;
        goto done;
    }

    ret = na_sm_process_cmd(na_sm_endpoint, cmd_hdr, -1, -1);
    NA_CHECK_SUBSYS_NA_ERROR(addr, done, ret, "Could not process cmd");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_process_cmd(struct na_sm_endpoint *na_sm_endpoint,
    union na_sm_cmd_hdr cmd_hdr, int tx_notify, int rx_notify)
{
    na_return_t ret = NA_SUCCESS;

    NA_LOG_SUBSYS_DEBUG(addr,
        "Processing cmd with %d from %d/%u/%u val=%" PRIu64, cmd_hdr.hdr.type,
        cmd_hdr.hdr.pid, cmd_hdr.hdr.id & 0xff, cmd_hdr.hdr.pair_idx & 0xff,
        cmd_hdr.val);

    switch (cmd_hdr.hdr.type) {
        case NA_SM_RESERVED: {
            struct na_sm_addr *na_sm_addr = NULL;
            struct na_sm_addr_key addr_key = {
                .pid = (pid_t) cmd_hdr.hdr.pid, .id = cmd_hdr.hdr.id};

            /* Allocate source address */
            ret = na_sm_addr_create(
                na_sm_endpoint, NULL, &addr_key, true, &na_sm_addr);
            NA_CHECK_SUBSYS_NA_ERROR(
                addr, done, ret, "Could not allocate unexpected address");

            na_sm_addr->shared_region =
                na_sm_endpoint->source_addr->shared_region;
            na_sm_addr->queue_pair_idx = cmd_hdr.hdr.pair_idx;

            /* Invert queues so that local rx is remote tx */
            na_sm_addr->tx_queue =
                &na_sm_addr->shared_region
                     ->queue_pairs[na_sm_addr->queue_pair_idx]
                     .rx_queue;
            na_sm_addr->rx_queue =
                &na_sm_addr->shared_region
                     ->queue_pairs[na_sm_addr->queue_pair_idx]
                     .tx_queue;

            /* Invert descriptors so that local rx is remote tx */
            na_sm_addr->tx_notify = rx_notify;
            na_sm_addr->rx_notify = tx_notify;

            if (na_sm_endpoint->poll_set && (na_sm_addr->rx_notify > 0)) {
                na_sm_addr->rx_poll_type = NA_SM_POLL_RX_NOTIFY;
                NA_LOG_SUBSYS_DEBUG(addr,
                    "Registering rx notify %d for polling",
                    na_sm_addr->rx_notify);
                /* Add remote rx notify to poll set */
                ret = na_sm_poll_register(na_sm_endpoint->poll_set,
                    na_sm_addr->rx_notify, &na_sm_addr->rx_poll_type);
                NA_CHECK_SUBSYS_NA_ERROR(
                    addr, done, ret, "Could not add rx notify to poll set");
            }

            /* Unexpected addresses are always resolved */
            hg_atomic_or32(&na_sm_addr->status, NA_SM_ADDR_RESOLVED);

            /* Add address to list of addresses to poll */
            hg_thread_spin_lock(&na_sm_endpoint->poll_addr_list.lock);
            LIST_INSERT_HEAD(
                &na_sm_endpoint->poll_addr_list.list, na_sm_addr, entry);
            hg_thread_spin_unlock(&na_sm_endpoint->poll_addr_list.lock);
            break;
        }
        case NA_SM_RELEASED: {
            struct na_sm_addr *na_sm_addr = NULL;
            bool found = false;

            /* Find address from list of addresses to poll */
            hg_thread_spin_lock(&na_sm_endpoint->poll_addr_list.lock);
            LIST_FOREACH (
                na_sm_addr, &na_sm_endpoint->poll_addr_list.list, entry) {
                if ((na_sm_addr->queue_pair_idx == cmd_hdr.hdr.pair_idx) &&
                    (na_sm_addr->addr_key.pid == (pid_t) cmd_hdr.hdr.pid) &&
                    (na_sm_addr->addr_key.id == cmd_hdr.hdr.id)) {
                    found = true;
                    break;
                }
            }
            hg_thread_spin_unlock(&na_sm_endpoint->poll_addr_list.lock);

            if (!found) {
                /* Silently ignore if not found */
                NA_LOG_SUBSYS_DEBUG(addr,
                    "Could not find address for PID=%d, ID=%u, pair_index=%u",
                    cmd_hdr.hdr.pid, cmd_hdr.hdr.id, cmd_hdr.hdr.pair_idx);
                break;
            }

            na_sm_addr_ref_decr(na_sm_addr);
            break;
        }
        default:
            NA_GOTO_SUBSYS_ERROR(
                poll, done, ret, NA_INVALID_ARG, "Unknown type of operation");
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress_tx_notify(struct na_sm_addr *poll_addr, bool *progressed)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* TODO we should be able to safely remove EFD_SEMAPHORE behavior */
    /* Local notification only */
    rc = hg_event_get(poll_addr->tx_notify, (bool *) progressed);
    NA_CHECK_SUBSYS_ERROR(msg, rc != HG_UTIL_SUCCESS, done, ret,
        na_sm_errno_to_na(errno), "Could not get completion notification");

    NA_LOG_SUBSYS_DEBUG(msg, "Progressed tx notify %d", poll_addr->tx_notify);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress_rx_notify(struct na_sm_addr *poll_addr, bool *progressed)
{
    na_return_t ret = NA_SUCCESS;

    /* TODO we should be able to safely remove EFD_SEMAPHORE behavior */
    /* Remote notification only */
    ret = na_sm_event_get(poll_addr->rx_notify, progressed);
    NA_CHECK_SUBSYS_NA_ERROR(
        msg, done, ret, "Could not get completion notification");

    NA_LOG_SUBSYS_DEBUG(msg, "Progressed rx notify %d", poll_addr->rx_notify);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress_rx_queue(struct na_sm_endpoint *na_sm_endpoint,
    struct na_sm_addr *poll_addr, bool *progressed)
{
    union na_sm_msg_hdr msg_hdr = {.val = 0};
    na_return_t ret = NA_SUCCESS;

    /* Look for message in rx queue */
    if (!na_sm_msg_queue_pop(poll_addr->rx_queue, &msg_hdr)) {
        *progressed = false;
        goto done;
    }

    NA_LOG_SUBSYS_DEBUG(msg, "Found msg in queue");

    /* Process expected and unexpected messages */
    switch (msg_hdr.hdr.type) {
        case NA_CB_SEND_UNEXPECTED:
            ret = na_sm_process_unexpected(&na_sm_endpoint->unexpected_op_queue,
                poll_addr, msg_hdr, &na_sm_endpoint->unexpected_msg_queue);
            NA_CHECK_SUBSYS_NA_ERROR(
                msg, done, ret, "Could not make progress on unexpected msg");
            break;
        case NA_CB_SEND_EXPECTED:
            na_sm_process_expected(
                &na_sm_endpoint->expected_op_queue, poll_addr, msg_hdr);
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(
                poll, done, ret, NA_INVALID_ARG, "Unknown type of operation");
    }

    *progressed = true;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_process_unexpected(struct na_sm_op_queue *unexpected_op_queue,
    struct na_sm_addr *poll_addr, union na_sm_msg_hdr msg_hdr,
    struct na_sm_unexpected_msg_queue *unexpected_msg_queue)
{
    struct na_sm_unexpected_info *na_sm_unexpected_info = NULL;
    struct na_sm_op_id *na_sm_op_id = NULL;
    na_return_t ret = NA_SUCCESS;

    NA_LOG_SUBSYS_DEBUG(msg, "Processing unexpected msg");

    /* Pop op ID from queue */
    hg_thread_spin_lock(&unexpected_op_queue->lock);
    na_sm_op_id = TAILQ_FIRST(&unexpected_op_queue->queue);
    if (likely(na_sm_op_id)) {
        TAILQ_REMOVE(&unexpected_op_queue->queue, na_sm_op_id, entry);
        hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_QUEUED);
    }
    hg_thread_spin_unlock(&unexpected_op_queue->lock);

    if (likely(na_sm_op_id)) {
        /* Fill info */
        na_sm_op_id->completion_data.callback_info.info.recv_unexpected =
            (struct na_cb_info_recv_unexpected){
                .tag = (na_tag_t) msg_hdr.hdr.tag,
                .actual_buf_size = (size_t) msg_hdr.hdr.buf_size,
                .source = (na_addr_t *) poll_addr};
        na_sm_addr_ref_incr(poll_addr);

        if (msg_hdr.hdr.buf_size > 0) {
            /* Copy buffer */
            na_sm_buf_copy_from(&poll_addr->shared_region->copy_bufs,
                msg_hdr.hdr.buf_idx, na_sm_op_id->info.msg.buf.ptr,
                msg_hdr.hdr.buf_size);

            /* Release buffer */
            na_sm_buf_release(
                &poll_addr->shared_region->copy_bufs, msg_hdr.hdr.buf_idx);
        }

        /* Complete operation (no need to notify) */
        na_sm_complete(na_sm_op_id, NA_SUCCESS);
    } else {
        NA_LOG_SUBSYS_WARNING(
            perf, "No operation was preposted, data must be copied");

        /* If no error and message arrived, keep a copy of the struct in
         * the unexpected message queue (should rarely happen) */
        na_sm_unexpected_info = (struct na_sm_unexpected_info *) malloc(
            sizeof(struct na_sm_unexpected_info));
        NA_CHECK_SUBSYS_ERROR(msg, na_sm_unexpected_info == NULL, done, ret,
            NA_NOMEM, "Could not allocate unexpected info");

        na_sm_unexpected_info->na_sm_addr = poll_addr;
        na_sm_unexpected_info->buf_size = (size_t) msg_hdr.hdr.buf_size;
        na_sm_unexpected_info->tag = (na_tag_t) msg_hdr.hdr.tag;

        if (na_sm_unexpected_info->buf_size > 0) {
            /* Allocate buf */
            na_sm_unexpected_info->buf =
                malloc(na_sm_unexpected_info->buf_size);
            NA_CHECK_SUBSYS_ERROR(msg, na_sm_unexpected_info->buf == NULL,
                error, ret, NA_NOMEM,
                "Could not allocate na_sm_unexpected_info buf");

            /* Copy buffer */
            na_sm_buf_copy_from(&poll_addr->shared_region->copy_bufs,
                msg_hdr.hdr.buf_idx, na_sm_unexpected_info->buf,
                msg_hdr.hdr.buf_size);

            /* Release buffer */
            na_sm_buf_release(
                &poll_addr->shared_region->copy_bufs, msg_hdr.hdr.buf_idx);
        } else
            na_sm_unexpected_info->buf = NULL;

        /* Otherwise push the unexpected message into our unexpected queue so
         * that we can treat it later when a recv_unexpected is posted */
        hg_thread_spin_lock(&unexpected_msg_queue->lock);
        STAILQ_INSERT_TAIL(
            &unexpected_msg_queue->queue, na_sm_unexpected_info, entry);
        hg_thread_spin_unlock(&unexpected_msg_queue->lock);
    }

done:
    return ret;

error:
    free(na_sm_unexpected_info);
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_process_expected(struct na_sm_op_queue *expected_op_queue,
    struct na_sm_addr *poll_addr, union na_sm_msg_hdr msg_hdr)
{
    struct na_sm_op_id *na_sm_op_id = NULL;

    NA_LOG_SUBSYS_DEBUG(msg, "Processing expected msg");

    /* Try to match addr/tag */
    hg_thread_spin_lock(&expected_op_queue->lock);
    TAILQ_FOREACH (na_sm_op_id, &expected_op_queue->queue, entry) {
        if (na_sm_op_id->addr == poll_addr &&
            na_sm_op_id->info.msg.tag == msg_hdr.hdr.tag) {
            TAILQ_REMOVE(&expected_op_queue->queue, na_sm_op_id, entry);
            hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_QUEUED);
            break;
        }
    }
    hg_thread_spin_unlock(&expected_op_queue->lock);

    /* If a message arrives without any OP ID being posted, drop it */
    if (na_sm_op_id == NULL) {
        NA_LOG_SUBSYS_WARNING(
            op, "No OP ID posted for that operation, dropping msg");
        if (msg_hdr.hdr.buf_size > 0) {
            /* Release buffer */
            na_sm_buf_release(
                &poll_addr->shared_region->copy_bufs, msg_hdr.hdr.buf_idx);
        }
        return;
    }

    na_sm_op_id->completion_data.callback_info.info.recv_expected
        .actual_buf_size = msg_hdr.hdr.buf_size;

    if (msg_hdr.hdr.buf_size > 0) {
        /* Copy buffer */
        na_sm_buf_copy_from(&poll_addr->shared_region->copy_bufs,
            msg_hdr.hdr.buf_idx, na_sm_op_id->info.msg.buf.ptr,
            msg_hdr.hdr.buf_size);

        /* Release buffer */
        na_sm_buf_release(
            &poll_addr->shared_region->copy_bufs, msg_hdr.hdr.buf_idx);
    }

    /* Complete operation */
    na_sm_complete(na_sm_op_id, NA_SUCCESS);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_process_retries(struct na_sm_endpoint *na_sm_endpoint)
{
    struct na_sm_op_queue *op_queue = &na_sm_endpoint->retry_op_queue;
    struct na_sm_op_id *na_sm_op_id = NULL;
    na_return_t ret = NA_SUCCESS;

    do {
        hg_thread_spin_lock(&op_queue->lock);
        na_sm_op_id = TAILQ_FIRST(&op_queue->queue);
        if (!na_sm_op_id) {
            hg_thread_spin_unlock(&op_queue->lock);
            /* Queue is empty */
            break;
        }
        /* We won't try to cancel an op that's being retried */
        hg_atomic_or32(&na_sm_op_id->status, NA_SM_OP_RETRYING);
        hg_thread_spin_unlock(&op_queue->lock);

        NA_LOG_SUBSYS_DEBUG(op, "Attempting to retry %p", (void *) na_sm_op_id);

        /* Attempt to resolve address first */
        ret = na_sm_msg_send_post(na_sm_endpoint,
            na_sm_op_id->completion_data.callback_info.type,
            na_sm_op_id->info.msg.buf.const_ptr, na_sm_op_id->info.msg.buf_size,
            na_sm_op_id->addr, na_sm_op_id->info.msg.tag);
        if (ret == NA_SUCCESS) {
            /* Succeeded, cannot cancel anymore */
            hg_thread_spin_lock(&op_queue->lock);
            hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_RETRYING);

            TAILQ_REMOVE(&op_queue->queue, na_sm_op_id, entry);
            hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_QUEUED);
            hg_thread_spin_unlock(&op_queue->lock);

            /* Immediate completion, add directly to completion queue. */
            na_sm_complete(na_sm_op_id, NA_SUCCESS);
        } else if (ret == NA_AGAIN) {
            bool canceled = false;

            /* Check if it was canceled in the meantime */
            hg_thread_spin_lock(&op_queue->lock);
            hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_RETRYING);

            if (hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_CANCELED) {
                TAILQ_REMOVE(&op_queue->queue, na_sm_op_id, entry);
                hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_QUEUED);
                canceled = true;
            }
            hg_thread_spin_unlock(&op_queue->lock);

            if (canceled)
                na_sm_complete(na_sm_op_id, NA_CANCELED);
            break; /* No need to try other op IDs */
        } else {
            NA_LOG_SUBSYS_ERROR(msg, "Could not post msg send operation");
            /* Force internal completion in error mode */
            hg_thread_spin_lock(&op_queue->lock);
            hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_RETRYING);
            hg_atomic_or32(&na_sm_op_id->status, NA_SM_OP_ERRORED);

            TAILQ_REMOVE(&op_queue->queue, na_sm_op_id, entry);
            hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_QUEUED);
            hg_thread_spin_unlock(&op_queue->lock);

            na_sm_complete(na_sm_op_id, ret);
            break; /* Better to return early ? */
        }
    } while (1);

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_op_retry(struct na_sm_class *na_sm_class, struct na_sm_op_id *na_sm_op_id)
{
    struct na_sm_op_queue *retry_op_queue =
        &na_sm_class->endpoint.retry_op_queue;

    NA_LOG_SUBSYS_DEBUG(op, "Pushing %p for retry (%s)", (void *) na_sm_op_id,
        na_cb_type_to_string(na_sm_op_id->completion_data.callback_info.type));

    /* Push op ID to retry queue */
    hg_thread_spin_lock(&retry_op_queue->lock);
    TAILQ_INSERT_TAIL(&retry_op_queue->queue, na_sm_op_id, entry);
    hg_atomic_or32(&na_sm_op_id->status, NA_SM_OP_QUEUED);
    hg_thread_spin_unlock(&retry_op_queue->lock);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_complete(struct na_sm_op_id *na_sm_op_id, na_return_t cb_ret)
{
    /* Mark op id as completed before checking for cancelation */
    hg_atomic_or32(&na_sm_op_id->status, NA_SM_OP_COMPLETED);

    /* Set callback ret */
    na_sm_op_id->completion_data.callback_info.ret = cb_ret;

    /* Add OP to NA completion queue */
    na_cb_completion_add(na_sm_op_id->context, &na_sm_op_id->completion_data);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_complete_signal(struct na_sm_class *na_sm_class)
{
    if (na_sm_class->endpoint.source_addr->tx_notify > 0) {
        int rc = hg_event_set(na_sm_class->endpoint.source_addr->tx_notify);
        NA_CHECK_SUBSYS_ERROR_DONE(
            op, rc != HG_UTIL_SUCCESS, "Could not signal completion");
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_sm_release(void *arg)
{
    struct na_sm_op_id *na_sm_op_id = (struct na_sm_op_id *) arg;

    NA_CHECK_SUBSYS_WARNING(op,
        na_sm_op_id &&
            (!(hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_COMPLETED)),
        "Releasing resources from an uncompleted operation");

    if (na_sm_op_id->addr) {
        na_sm_addr_ref_decr(na_sm_op_id->addr);
        na_sm_op_id->addr = NULL;
    }
}

/********************/
/* Plugin callbacks */
/********************/

static na_return_t
na_sm_get_protocol_info(
    const struct na_info *na_info, struct na_protocol_info **na_protocol_info_p)
{
    const char *protocol_name =
        (na_info != NULL) ? na_info->protocol_name : NULL;
    na_return_t ret;

    if (protocol_name != NULL && strcmp(protocol_name, "sm")) {
        *na_protocol_info_p = NULL;
        return NA_SUCCESS;
    }

    *na_protocol_info_p = na_protocol_info_alloc("na", "sm", "shm");
    NA_CHECK_SUBSYS_ERROR(cls, *na_protocol_info_p == NULL, error, ret,
        NA_NOMEM, "Could not allocate protocol info entry");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static bool
na_sm_check_protocol(const char *protocol_name)
{
    bool accept = false;

    if (!strcmp("sm", protocol_name))
        accept = true;

    return accept;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen)
{
    const struct na_init_info *na_init_info = &na_info->na_init_info;
    struct na_sm_class *na_sm_class = NULL;
    struct rlimit rlimit;
    na_return_t ret;
    int rc;

    /* Reset errno */
    errno = 0;

    /* Check RLIMIT_NOFILE */
    rc = getrlimit(RLIMIT_NOFILE, &rlimit);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_sm_errno_to_na(errno),
        "getrlimit() failed (%s)", strerror(errno));

    NA_LOG_SUBSYS_DEBUG(cls, "RLIMIT_NOFILE is: %ju, max %ju",
        (uintmax_t) rlimit.rlim_cur, (uintmax_t) rlimit.rlim_max);

    /* Initialize private data */
    na_sm_class = (struct na_sm_class *) calloc(1, sizeof(*na_sm_class));
    NA_CHECK_SUBSYS_ERROR(cls, na_sm_class == NULL, error, ret, NA_NOMEM,
        "Could not allocate SM private class");

#ifdef NA_SM_HAS_CMA
    na_sm_class->iov_max = (size_t) sysconf(_SC_IOV_MAX);
#else
    na_sm_class->iov_max = 1;
#endif
    na_sm_class->context_max = na_init_info->max_contexts;

    /* Open endpoint */
    ret = na_sm_endpoint_open(&na_sm_class->endpoint, na_info->host_name,
        listen, na_init_info->progress_mode & NA_NO_BLOCK,
        (uint32_t) rlimit.rlim_cur);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not open endpoint");

    na_class->plugin_class = (void *) na_sm_class;

    return NA_SUCCESS;

error:
    free(na_sm_class);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_finalize(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;

    if (!na_class->plugin_class)
        goto done;

    NA_LOG_SUBSYS_DEBUG(cls, "Closing endpoint");

    /* Close endpoint */
    ret = na_sm_endpoint_close(&NA_SM_CLASS(na_class)->endpoint);
    NA_CHECK_SUBSYS_NA_ERROR(cls, done, ret, "Could not close endpoint");

    free(na_class->plugin_class);
    na_class->plugin_class = NULL;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_context_create(
    na_class_t NA_UNUSED *na_class, void **context_p, uint8_t NA_UNUSED id)
{
    na_return_t ret = NA_SUCCESS;

    *context_p = malloc(sizeof(struct na_sm_context));
    NA_CHECK_SUBSYS_ERROR(ctx, *context_p == NULL, done, ret, NA_NOMEM,
        "Could not allocate SM private context");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_context_destroy(na_class_t NA_UNUSED *na_class, void *context)
{
    free(context);

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_cleanup(void)
{
    int rc;

    /* We need to remove all files first before being able to remove the
     * directories */
    rc = nftw(NA_SM_TMP_DIRECTORY, na_sm_sock_path_cleanup, NA_SM_CLEANUP_NFDS,
        FTW_PHYS | FTW_DEPTH);
    NA_CHECK_SUBSYS_WARNING(
        cls, rc != 0 && errno != ENOENT, "nftw() failed (%s)", strerror(errno));

    rc = nftw(NA_SM_SHM_PATH, na_sm_shm_cleanup, NA_SM_CLEANUP_NFDS, FTW_PHYS);
    NA_CHECK_SUBSYS_WARNING(
        cls, rc != 0 && errno != ENOENT, "nftw() failed (%s)", strerror(errno));
}

/*---------------------------------------------------------------------------*/
static na_op_id_t *
na_sm_op_create(na_class_t *na_class, unsigned long NA_UNUSED flags)
{
    struct na_sm_op_id *na_sm_op_id = NULL;

    na_sm_op_id = (struct na_sm_op_id *) malloc(sizeof(struct na_sm_op_id));
    NA_CHECK_SUBSYS_ERROR_NORET(
        op, na_sm_op_id == NULL, done, "Could not allocate NA SM operation ID");
    memset(na_sm_op_id, 0, sizeof(struct na_sm_op_id));

    na_sm_op_id->na_class = na_class;

    /* Completed by default */
    hg_atomic_init32(&na_sm_op_id->status, NA_SM_OP_COMPLETED);

    /* Set op ID release callbacks */
    na_sm_op_id->completion_data.plugin_callback = na_sm_release;
    na_sm_op_id->completion_data.plugin_callback_args = na_sm_op_id;

done:
    return (na_op_id_t *) na_sm_op_id;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_op_destroy(na_class_t NA_UNUSED *na_class, na_op_id_t *op_id)
{
    struct na_sm_op_id *na_sm_op_id = (struct na_sm_op_id *) op_id;

    NA_CHECK_SUBSYS_WARNING(op,
        !(hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_COMPLETED),
        "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_sm_op_id->completion_data.callback_info.type));

    free(na_sm_op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr_p)
{
    struct na_sm_endpoint *na_sm_endpoint = &NA_SM_CLASS(na_class)->endpoint;
    struct na_sm_addr *na_sm_addr = NULL;
    char uri[NA_SM_MAX_FILENAME];
    struct na_sm_addr_key addr_key;
    na_return_t ret = NA_SUCCESS;

    /* Extra info from string */
    ret = na_sm_string_to_addr(name, uri, sizeof(uri), &addr_key);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not convert string (%s) to address", name);

    NA_LOG_SUBSYS_DEBUG(
        addr, "Lookup addr for PID=%d, ID=%" PRIu8, addr_key.pid, addr_key.id);

    /* Lookup addr from hash table */
    na_sm_addr = na_sm_addr_map_lookup(&na_sm_endpoint->addr_map, &addr_key);
    if (!na_sm_addr) {
        na_return_t na_ret;
        int rc;

        NA_LOG_SUBSYS_DEBUG(addr,
            "Address for PID=%d, ID=%" PRIu8
            " was not found, attempting to insert it",
            addr_key.pid, addr_key.id);

        /* Re-generate URI */
        rc = NA_SM_PRINT_URI(uri, NA_SM_MAX_FILENAME, addr_key);
        NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > NA_SM_MAX_FILENAME, error,
            ret, NA_OVERFLOW, "NA_SM_PRINT_URI() failed, rc: %d", rc);

        /* Insert new entry and create new address if needed */
        na_ret = na_sm_addr_map_insert(na_sm_endpoint,
            &na_sm_endpoint->addr_map, uri, &addr_key, &na_sm_addr);
        NA_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS && na_ret != NA_EXIST,
            error, ret, na_ret, "Could not insert new address");
    } else {
        NA_LOG_SUBSYS_DEBUG(addr, "Address for PID=%d, ID=%" PRIu8 " was found",
            addr_key.pid, addr_key.id);
    }

    /* Increment refcount */
    na_sm_addr_ref_incr(na_sm_addr);

    *addr_p = (na_addr_t *) na_sm_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_addr_free(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    na_sm_addr_ref_decr((struct na_sm_addr *) addr);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_self(na_class_t *na_class, na_addr_t **addr_p)
{
    na_sm_addr_ref_incr(NA_SM_CLASS(na_class)->endpoint.source_addr);
    *addr_p = (na_addr_t *) NA_SM_CLASS(na_class)->endpoint.source_addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_dup(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr, na_addr_t **new_addr_p)
{
    na_sm_addr_ref_incr((struct na_sm_addr *) addr);
    *new_addr_p = addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static bool
na_sm_addr_cmp(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr1, na_addr_t *addr2)
{
    struct na_sm_addr *na_sm_addr1 = (struct na_sm_addr *) addr1;
    struct na_sm_addr *na_sm_addr2 = (struct na_sm_addr *) addr2;

    return (na_sm_addr1->addr_key.pid == na_sm_addr2->addr_key.pid) &&
           (na_sm_addr1->addr_key.id == na_sm_addr2->addr_key.id);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_sm_addr_is_self(na_class_t *na_class, na_addr_t *addr)
{
    return na_sm_addr_cmp(na_class,
        (na_addr_t *) NA_SM_CLASS(na_class)->endpoint.source_addr, addr);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_to_string(na_class_t NA_UNUSED *na_class, char *buf,
    size_t *buf_size, na_addr_t *addr)
{
    struct na_sm_addr *na_sm_addr = (struct na_sm_addr *) addr;
    size_t string_len;
    char uri[NA_SM_MAX_FILENAME], *uri_p;
    char addr_string[NA_SM_MAX_FILENAME] = {'\0'};
    na_return_t ret = NA_SUCCESS;
    int rc;

    if (na_sm_addr->uri == NULL) {
        rc = NA_SM_PRINT_URI(uri, NA_SM_MAX_FILENAME, na_sm_addr->addr_key);
        NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > NA_SM_MAX_FILENAME, done,
            ret, NA_OVERFLOW, "NA_SM_PRINT_URI() failed, rc: %d", rc);
        uri_p = uri;
    } else
        uri_p = na_sm_addr->uri;

    rc = snprintf(addr_string, NA_SM_MAX_FILENAME, "sm://%s", uri_p);
    NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > NA_SM_MAX_FILENAME, done, ret,
        NA_OVERFLOW, "snprintf() failed, rc: %d", rc);

    string_len = strlen(addr_string);
    if (buf) {
        NA_CHECK_SUBSYS_ERROR(addr, string_len >= *buf_size, done, ret,
            NA_OVERFLOW, "Buffer size too small to copy addr");
        strcpy(buf, addr_string);
    }
    *buf_size = string_len + 1;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_sm_addr_get_serialize_size(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    return sizeof(((struct na_sm_addr *) addr)->addr_key);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_serialize(
    na_class_t NA_UNUSED *na_class, void *buf, size_t buf_size, na_addr_t *addr)
{
    struct na_sm_addr *na_sm_addr = (struct na_sm_addr *) addr;
    char *buf_ptr = (char *) buf;
    size_t buf_size_left = buf_size;
    na_return_t ret = NA_SUCCESS;

    /* Encode addr key */
    NA_ENCODE(done, ret, buf_ptr, buf_size_left, &na_sm_addr->addr_key,
        struct na_sm_addr_key);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_deserialize(
    na_class_t *na_class, na_addr_t **addr_p, const void *buf, size_t buf_size)
{
    struct na_sm_endpoint *na_sm_endpoint = &NA_SM_CLASS(na_class)->endpoint;
    struct na_sm_addr *na_sm_addr = NULL;
    struct na_sm_addr_key addr_key;
    const char *buf_ptr = (const char *) buf;
    size_t buf_size_left = buf_size;
    na_return_t ret = NA_SUCCESS;

    /* Decode addr key */
    NA_DECODE(
        done, ret, buf_ptr, buf_size_left, &addr_key, struct na_sm_addr_key);

    /* Lookup addr from hash table */
    na_sm_addr = na_sm_addr_map_lookup(&na_sm_endpoint->addr_map, &addr_key);
    if (!na_sm_addr) {
        na_return_t na_ret;

        NA_LOG_SUBSYS_DEBUG(addr,
            "Address for PID=%d, ID=%" PRIu8
            " was not found, attempting to insert it",
            addr_key.pid, addr_key.id);

        /* Insert new entry and create new address if needed */
        na_ret = na_sm_addr_map_insert(na_sm_endpoint,
            &na_sm_endpoint->addr_map, NULL, &addr_key, &na_sm_addr);
        NA_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS && na_ret != NA_EXIST,
            done, ret, na_ret, "Could not insert new address");
    } else {
        NA_LOG_SUBSYS_DEBUG(addr, "Address for PID=%d, ID=%" PRIu8 " was found",
            addr_key.pid, addr_key.id);
    }

    /* Increment refcount */
    na_sm_addr_ref_incr(na_sm_addr);

    *addr_p = (na_addr_t *) na_sm_addr;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_sm_msg_get_max_unexpected_size(const na_class_t NA_UNUSED *na_class)
{
    return NA_SM_UNEXPECTED_SIZE;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_sm_msg_get_max_expected_size(const na_class_t NA_UNUSED *na_class)
{
    return NA_SM_EXPECTED_SIZE;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_tag_t
na_sm_msg_get_max_tag(const na_class_t NA_UNUSED *na_class)
{
    return NA_SM_MAX_TAG;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    return na_sm_msg_send(NA_SM_CLASS(na_class), context, NA_CB_SEND_UNEXPECTED,
        callback, arg, buf, buf_size, (struct na_sm_addr *) dest_addr, tag,
        (struct na_sm_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_op_id_t *op_id)
{
    struct na_sm_unexpected_msg_queue *unexpected_msg_queue =
        &NA_SM_CLASS(na_class)->endpoint.unexpected_msg_queue;
    struct na_sm_unexpected_info *na_sm_unexpected_info;
    struct na_sm_op_id *na_sm_op_id = (struct na_sm_op_id *) op_id;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(msg, buf_size > NA_SM_UNEXPECTED_SIZE, error, ret,
        NA_OVERFLOW, "Exceeds unexpected size, %zu", buf_size);

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_sm_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_sm_op_id->completion_data.callback_info.type));

    NA_SM_OP_RESET_UNEXPECTED_RECV(na_sm_op_id, context, callback, arg);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_sm_op_id->info.msg =
        (struct na_sm_msg_info){.buf.ptr = buf, .buf_size = buf_size, .tag = 0};

    /* Look for an unexpected message already received */
    hg_thread_spin_lock(&unexpected_msg_queue->lock);
    na_sm_unexpected_info = STAILQ_FIRST(&unexpected_msg_queue->queue);
    if (na_sm_unexpected_info != NULL)
        STAILQ_REMOVE_HEAD(&unexpected_msg_queue->queue, entry);
    hg_thread_spin_unlock(&unexpected_msg_queue->lock);

    if (unlikely(na_sm_unexpected_info)) {
        /* Fill unexpected info */
        na_sm_op_id->completion_data.callback_info.info.recv_unexpected =
            (struct na_cb_info_recv_unexpected){
                .tag = (na_tag_t) na_sm_unexpected_info->tag,
                .actual_buf_size = (size_t) na_sm_unexpected_info->buf_size,
                .source = (na_addr_t *) na_sm_unexpected_info->na_sm_addr};
        na_sm_addr_ref_incr(na_sm_unexpected_info->na_sm_addr);

        if (na_sm_unexpected_info->buf_size > 0) {
            /* Copy buffers */
            memcpy(na_sm_op_id->info.msg.buf.ptr, na_sm_unexpected_info->buf,
                na_sm_unexpected_info->buf_size);
            free(na_sm_unexpected_info->buf);
        }
        free(na_sm_unexpected_info);
        na_sm_complete(na_sm_op_id, NA_SUCCESS);

        /* Notify local completion */
        na_sm_complete_signal(NA_SM_CLASS(na_class));
    } else {
        struct na_sm_op_queue *unexpected_op_queue =
            &NA_SM_CLASS(na_class)->endpoint.unexpected_op_queue;

        /* Nothing has been received yet so add op_id to progress queue */
        hg_thread_spin_lock(&unexpected_op_queue->lock);
        TAILQ_INSERT_TAIL(&unexpected_op_queue->queue, na_sm_op_id, entry);
        hg_atomic_or32(&na_sm_op_id->status, NA_SM_OP_QUEUED);
        hg_thread_spin_unlock(&unexpected_op_queue->lock);
    }

    return NA_SUCCESS;

    /* nothing that requires release
    release:
        NA_SM_OP_RELEASE(na_sm_op_id);
    */

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    return na_sm_msg_send(NA_SM_CLASS(na_class), context, NA_CB_SEND_EXPECTED,
        callback, arg, buf, buf_size, (struct na_sm_addr *) dest_addr, tag,
        (struct na_sm_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *source_addr,
    uint8_t NA_UNUSED source_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_sm_op_queue *expected_op_queue =
        &NA_SM_CLASS(na_class)->endpoint.expected_op_queue;
    struct na_sm_op_id *na_sm_op_id = (struct na_sm_op_id *) op_id;
    struct na_sm_addr *na_sm_addr = (struct na_sm_addr *) source_addr;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(msg, buf_size > NA_SM_EXPECTED_SIZE, error, ret,
        NA_OVERFLOW, "Exceeds expected size, %zu", buf_size);

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_sm_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_sm_op_id->completion_data.callback_info.type));

    NA_SM_OP_RESET(
        na_sm_op_id, context, NA_CB_RECV_EXPECTED, callback, arg, na_sm_addr);

    /* TODO we assume that buf remains valid (safe because we pre-allocate
     * buffers) */
    na_sm_op_id->info.msg = (struct na_sm_msg_info){
        .buf.ptr = buf, .buf_size = buf_size, .tag = tag};

    /* Expected messages must always be pre-posted, therefore a message should
     * never arrive before that call returns (not completes), simply add
     * op_id to queue */
    hg_thread_spin_lock(&expected_op_queue->lock);
    TAILQ_INSERT_TAIL(&expected_op_queue->queue, na_sm_op_id, entry);
    hg_atomic_or32(&na_sm_op_id->status, NA_SM_OP_QUEUED);
    hg_thread_spin_unlock(&expected_op_queue->lock);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_create(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, unsigned long flags, na_mem_handle_t **mem_handle_p)
{
    struct na_sm_mem_handle *na_sm_mem_handle = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Allocate memory handle */
    na_sm_mem_handle =
        (struct na_sm_mem_handle *) calloc(1, sizeof(struct na_sm_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_sm_mem_handle == NULL, done, ret, NA_NOMEM,
        "Could not allocate NA SM memory handle");

    na_sm_mem_handle->iov.s[0] =
        (struct iovec){.iov_base = buf, .iov_len = buf_size};
    na_sm_mem_handle->info.iovcnt = 1;
    na_sm_mem_handle->info.flags = flags & 0xff;
    na_sm_mem_handle->info.len = buf_size;

    *mem_handle_p = (na_mem_handle_t *) na_sm_mem_handle;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
#ifdef NA_SM_HAS_CMA
static na_return_t
na_sm_mem_handle_create_segments(na_class_t *na_class,
    struct na_segment *segments, size_t segment_count, unsigned long flags,
    na_mem_handle_t **mem_handle_p)
{
    struct na_sm_mem_handle *na_sm_mem_handle = NULL;
    struct iovec *iov = NULL;
    na_return_t ret = NA_SUCCESS;
    size_t i;

    NA_CHECK_SUBSYS_WARNING(mem, segment_count == 1, "Segment count is 1");

    /* Check that we do not exceed IOV_MAX */
    NA_CHECK_SUBSYS_ERROR(fatal, segment_count > NA_SM_CLASS(na_class)->iov_max,
        error, ret, NA_INVALID_ARG, "Segment count exceeds IOV_MAX limit (%zu)",
        NA_SM_CLASS(na_class)->iov_max);

    /* Allocate memory handle */
    na_sm_mem_handle =
        (struct na_sm_mem_handle *) calloc(1, sizeof(struct na_sm_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_sm_mem_handle == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA SM memory handle");

    if (segment_count > NA_SM_IOV_STATIC_MAX) {
        /* Allocate IOVs */
        na_sm_mem_handle->iov.d =
            (struct iovec *) calloc(segment_count, sizeof(struct iovec));
        NA_CHECK_SUBSYS_ERROR(mem, na_sm_mem_handle->iov.d == NULL, error, ret,
            NA_NOMEM, "Could not allocate iovec");

        iov = na_sm_mem_handle->iov.d;
    } else
        iov = na_sm_mem_handle->iov.s;

    na_sm_mem_handle->info.len = 0;
    for (i = 0; i < segment_count; i++) {
        iov[i].iov_base = (void *) segments[i].base;
        iov[i].iov_len = segments[i].len;
        na_sm_mem_handle->info.len += iov[i].iov_len;
    }
    na_sm_mem_handle->info.iovcnt = segment_count;
    na_sm_mem_handle->info.flags = flags & 0xff;

    *mem_handle_p = (na_mem_handle_t *) na_sm_mem_handle;

    return ret;

error:
    if (na_sm_mem_handle) {
        if (segment_count > NA_SM_IOV_STATIC_MAX)
            free(na_sm_mem_handle->iov.d);
        free(na_sm_mem_handle);
    }
    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
static void
na_sm_mem_handle_free(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    struct na_sm_mem_handle *na_sm_mem_handle =
        (struct na_sm_mem_handle *) mem_handle;

    if (na_sm_mem_handle->info.iovcnt > NA_SM_IOV_STATIC_MAX)
        free(na_sm_mem_handle->iov.d);
    free(na_sm_mem_handle);
}

/*---------------------------------------------------------------------------*/
static size_t
na_sm_mem_handle_get_max_segments(const na_class_t *na_class)
{
    return NA_SM_CLASS(na_class)->iov_max;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_sm_mem_handle_get_serialize_size(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    struct na_sm_mem_handle *na_sm_mem_handle =
        (struct na_sm_mem_handle *) mem_handle;

    return sizeof(na_sm_mem_handle->info) +
           na_sm_mem_handle->info.iovcnt * sizeof(struct iovec);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_serialize(na_class_t NA_UNUSED *na_class, void *buf,
    size_t NA_UNUSED buf_size, na_mem_handle_t *mem_handle)
{
    struct na_sm_mem_handle *na_sm_mem_handle =
        (struct na_sm_mem_handle *) mem_handle;
    struct iovec *iov = NA_SM_IOV(na_sm_mem_handle);
    char *buf_ptr = (char *) buf;
    size_t buf_size_left = buf_size;
    na_return_t ret = NA_SUCCESS;

    /* Descriptor info */
    NA_ENCODE(done, ret, buf_ptr, buf_size_left, &na_sm_mem_handle->info,
        struct na_sm_mem_desc_info);

    /* IOV */
    NA_ENCODE_ARRAY(done, ret, buf_ptr, buf_size_left, iov, struct iovec,
        na_sm_mem_handle->info.iovcnt);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_deserialize(na_class_t NA_UNUSED *na_class,
    na_mem_handle_t **mem_handle_p, const void *buf, NA_UNUSED size_t buf_size)
{
    struct na_sm_mem_handle *na_sm_mem_handle = NULL;
    const char *buf_ptr = (const char *) buf;
    size_t buf_size_left = buf_size;
    struct iovec *iov = NULL;
    na_return_t ret = NA_SUCCESS;

    na_sm_mem_handle =
        (struct na_sm_mem_handle *) malloc(sizeof(struct na_sm_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_sm_mem_handle == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA SM memory handle");
    na_sm_mem_handle->iov.d = NULL;
    na_sm_mem_handle->info.iovcnt = 0;

    /* Descriptor info */
    NA_DECODE(error, ret, buf_ptr, buf_size_left, &na_sm_mem_handle->info,
        struct na_sm_mem_desc_info);

    /* IOV */
    if (na_sm_mem_handle->info.iovcnt > NA_SM_IOV_STATIC_MAX) {
        /* Allocate IOV */
        na_sm_mem_handle->iov.d = (struct iovec *) malloc(
            na_sm_mem_handle->info.iovcnt * sizeof(struct iovec));
        NA_CHECK_SUBSYS_ERROR(mem, na_sm_mem_handle->iov.d == NULL, error, ret,
            NA_NOMEM, "Could not allocate segment array");

        iov = na_sm_mem_handle->iov.d;
    } else
        iov = na_sm_mem_handle->iov.s;

    NA_DECODE_ARRAY(error, ret, buf_ptr, buf_size_left, iov, struct iovec,
        na_sm_mem_handle->info.iovcnt);

    *mem_handle_p = (na_mem_handle_t *) na_sm_mem_handle;

    return ret;

error:
    if (na_sm_mem_handle) {
        if (na_sm_mem_handle->info.iovcnt > NA_SM_IOV_STATIC_MAX)
            free(na_sm_mem_handle->iov.d);
        free(na_sm_mem_handle);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_sm_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    return na_sm_rma(NA_SM_CLASS(na_class), context, NA_CB_PUT, callback, arg,
        na_sm_process_vm_writev, (struct na_sm_mem_handle *) local_mem_handle,
        local_offset, (struct na_sm_mem_handle *) remote_mem_handle,
        remote_offset, length, (struct na_sm_addr *) remote_addr,
        (struct na_sm_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_sm_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    return na_sm_rma(NA_SM_CLASS(na_class), context, NA_CB_GET, callback, arg,
        na_sm_process_vm_readv, (struct na_sm_mem_handle *) local_mem_handle,
        local_offset, (struct na_sm_mem_handle *) remote_mem_handle,
        remote_offset, length, (struct na_sm_addr *) remote_addr,
        (struct na_sm_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_sm_poll_get_fd(na_class_t *na_class, na_context_t NA_UNUSED *context)
{
    int fd = -1;

    if (NA_SM_CLASS(na_class)->endpoint.poll_set) {
        fd = hg_poll_get_fd(NA_SM_CLASS(na_class)->endpoint.poll_set);
        NA_CHECK_SUBSYS_ERROR_NORET(
            poll, fd == -1, done, "Could not get poll fd from poll set");
    }

done:
    return fd;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_sm_poll_try_wait(na_class_t *na_class, na_context_t NA_UNUSED *context)
{
    struct na_sm_endpoint *na_sm_endpoint = &NA_SM_CLASS(na_class)->endpoint;
    struct na_sm_addr *na_sm_addr;
    bool empty = false;

    /* Check whether something is in one of the rx queues */
    hg_thread_spin_lock(&na_sm_endpoint->poll_addr_list.lock);
    LIST_FOREACH (na_sm_addr, &na_sm_endpoint->poll_addr_list.list, entry) {
        if (!na_sm_msg_queue_is_empty(na_sm_addr->rx_queue)) {
            hg_thread_spin_unlock(&na_sm_endpoint->poll_addr_list.lock);
            return false;
        }
    }
    hg_thread_spin_unlock(&na_sm_endpoint->poll_addr_list.lock);

    /* Check whether something is in the retry queue */
    hg_thread_spin_lock(&na_sm_endpoint->retry_op_queue.lock);
    empty = TAILQ_EMPTY(&na_sm_endpoint->retry_op_queue.queue);
    hg_thread_spin_unlock(&na_sm_endpoint->retry_op_queue.lock);
    if (!empty)
        return false;

    return true;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p)
{
    struct na_sm_endpoint *na_sm_endpoint = &NA_SM_CLASS(na_class)->endpoint;
    unsigned int count = 0;
    na_return_t ret;

    if (na_sm_endpoint->poll_set) {
        /* Make blocking progress */
        ret = na_sm_progress_wait(context, na_sm_endpoint, 0, &count);
        NA_CHECK_SUBSYS_NA_ERROR(
            poll, error, ret, "Could not make blocking progress on context");
    } else {
        /* Make non-blocking progress */
        ret = na_sm_progress(na_sm_endpoint, &count);
        NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret,
            "Could not make non-blocking progress on context");
    }

    /* Process retries */
    ret = na_sm_process_retries(&NA_SM_CLASS(na_class)->endpoint);
    NA_CHECK_SUBSYS_NA_ERROR(
        poll, error, ret, "Could not process retried msgs");

    if (count_p != NULL)
        *count_p = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_poll_wait(na_class_t *na_class, na_context_t *context,
    unsigned int timeout_ms, unsigned int *count_p)
{
    struct na_sm_endpoint *na_sm_endpoint = &NA_SM_CLASS(na_class)->endpoint;
    hg_time_t deadline, now = hg_time_from_ms(0);
    na_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    do {
        unsigned int count = 0;

        if (na_sm_endpoint->poll_set) {
            /* Make blocking progress */
            ret = na_sm_progress_wait(context, na_sm_endpoint,
                hg_time_to_ms(hg_time_subtract(deadline, now)), &count);
            NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret,
                "Could not make blocking progress on context");
        } else {
            /* Make non-blocking progress */
            ret = na_sm_progress(na_sm_endpoint, &count);
            NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret,
                "Could not make non-blocking progress on context");
        }

        /* Process retries */
        ret = na_sm_process_retries(&NA_SM_CLASS(na_class)->endpoint);
        NA_CHECK_SUBSYS_NA_ERROR(
            poll, error, ret, "Could not process retried msgs");

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
na_sm_cancel(
    na_class_t *na_class, na_context_t NA_UNUSED *context, na_op_id_t *op_id)
{
    struct na_sm_op_id *na_sm_op_id = (struct na_sm_op_id *) op_id;
    struct na_sm_op_queue *op_queue = NULL;
    int32_t status;
    na_return_t ret;

    /* Exit if op has already completed */
    status = hg_atomic_get32(&na_sm_op_id->status);
    if ((status & NA_SM_OP_COMPLETED) || (status & NA_SM_OP_ERRORED) ||
        (status & NA_SM_OP_CANCELED))
        return NA_SUCCESS;

    NA_LOG_SUBSYS_DEBUG(op, "Canceling operation ID %p (%s)",
        (void *) na_sm_op_id,
        na_cb_type_to_string(na_sm_op_id->completion_data.callback_info.type));

    switch (na_sm_op_id->completion_data.callback_info.type) {
        case NA_CB_RECV_UNEXPECTED:
            /* Must remove op_id from unexpected op queue */
            op_queue = &NA_SM_CLASS(na_class)->endpoint.unexpected_op_queue;
            break;
        case NA_CB_RECV_EXPECTED:
            /* Must remove op_id from unexpected op queue */
            op_queue = &NA_SM_CLASS(na_class)->endpoint.expected_op_queue;
            break;
        case NA_CB_SEND_UNEXPECTED:
        case NA_CB_SEND_EXPECTED:
            /* Must remove op_id from retry op queue */
            op_queue = &NA_SM_CLASS(na_class)->endpoint.retry_op_queue;
            break;
        case NA_CB_PUT:
        case NA_CB_GET:
            /* Nothing */
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(op, error, ret, NA_INVALID_ARG,
                "Operation type %d not supported",
                na_sm_op_id->completion_data.callback_info.type);
    }

    /* Remove op id from queue it is on */
    if (op_queue) {
        bool canceled = false;

        hg_thread_spin_lock(&op_queue->lock);
        if (hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_QUEUED) {
            hg_atomic_or32(&na_sm_op_id->status, NA_SM_OP_CANCELED);

            /* If being retried by process_retries() in the meantime, we'll just
             * let it cancel there */
            if (!(hg_atomic_get32(&na_sm_op_id->status) & NA_SM_OP_RETRYING)) {
                TAILQ_REMOVE(&op_queue->queue, na_sm_op_id, entry);
                hg_atomic_and32(&na_sm_op_id->status, ~NA_SM_OP_QUEUED);
                canceled = true;
            }
        }
        hg_thread_spin_unlock(&op_queue->lock);

        /* Cancel op id */
        if (canceled) {
            na_sm_complete(na_sm_op_id, NA_CANCELED);

            na_sm_complete_signal(NA_SM_CLASS(na_class));
        }
    }

    return NA_SUCCESS;

error:
    return ret;
}
