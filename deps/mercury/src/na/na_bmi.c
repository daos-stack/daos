/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_plugin.h"

#include "na_ip.h"

#include "mercury_hash_table.h"
#include "mercury_thread_mutex.h"
#include "mercury_thread_rwlock.h"
#include "mercury_thread_spin.h"
#include "mercury_time.h"

#include <bmi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/****************/
/* Local Macros */
/****************/

/* Max addr name */
#define NA_BMI_ADDR_NAME_MAX (256)
#define NA_BMI_ADDR_PREALLOC (64)

/* Msg sizes */
#define NA_BMI_UNEXPECTED_SIZE (4096)
#define NA_BMI_EXPECTED_SIZE   NA_BMI_UNEXPECTED_SIZE

/* Default tags, take the first 3 bits for RMA handling
 * RMA msg - RMA op - RMA ack - remaining bits for tags
 * do not use bit 31 as bmi_msg_tag_t is signed.
 */
#define NA_BMI_RMA_MSG_TAG (1 << 28)
#define NA_BMI_RMA_TAG     (1 << 29)
#define NA_BMI_RMA_ACK_TAG (1 << 30)

/* Max tag used for messages (all above bits are not set) */
#define NA_BMI_TAG_MAX ((1 << 28) - 1)

/* RMA flags */
#define NA_BMI_RMA_SVC (1 << 0)
#define NA_BMI_RMA_ACK (1 << 1)

/* Op ID status bits */
#define NA_BMI_OP_COMPLETED (1 << 0)
#define NA_BMI_OP_CANCELED  (1 << 1)
#define NA_BMI_OP_QUEUED    (1 << 2)
#define NA_BMI_OP_ERRORED   (1 << 3)

#define NA_BMI_CLASS(na_class)                                                 \
    ((struct na_bmi_class *) (na_class->plugin_class))
#define NA_BMI_CONTEXT(context)                                                \
    ((struct na_bmi_context *) (context->plugin_context))

/************************************/
/* Local Type and Struct Definition */
/************************************/

/* na_bmi_addr */
struct na_bmi_addr {
    STAILQ_ENTRY(na_bmi_addr) entry; /* Pool of addresses */
    BMI_addr_t bmi_addr;             /* BMI addr */
    bool unexpected;                 /* From unexpected recv */
    bool self;                       /* Boolean for self */
    hg_atomic_int32_t ref_count;     /* Ref count */
};

struct na_bmi_unexpected_info {
    struct BMI_unexpected_info info;
    struct na_bmi_addr *na_bmi_addr;
    STAILQ_ENTRY(na_bmi_unexpected_info) entry;
};

struct na_bmi_mem_handle {
    void *base;     /* Base address of region */
    bmi_size_t len; /* Size of region */
    uint8_t flags;  /* Flag of operation access */
};

typedef enum na_bmi_rma_op {
    NA_BMI_RMA_PUT, /* Request a put operation */
    NA_BMI_RMA_GET  /* Request a get operation */
} na_bmi_rma_op_t;

struct na_bmi_rma_msg_info {
    na_bmi_rma_op_t op;    /* Operation requested */
    void *base;            /* Base address of region */
    bmi_size_t len;        /* Length of region */
    bmi_msg_tag_t rma_tag; /* Tag used for the data transfer */
    bmi_msg_tag_t ack_tag; /* Tag used for completion ack */
};

/* Msg info */
struct na_bmi_msg_info {
    union {
        const void *const_ptr;
        void *ptr;
    } buf;
    bmi_size_t buf_size;
    bmi_size_t actual_buf_size;
    bmi_msg_tag_t tag;
    bmi_op_id_t op_id;
};

/* RMA info */
struct na_bmi_rma_info {
    struct na_bmi_rma_msg_info msg_info;
    void *base;
    bmi_size_t actual_len;
    bmi_size_t ack_size;
    bmi_op_id_t msg_op_id;
    bmi_op_id_t rma_op_id;
    bmi_op_id_t ack_op_id;
    hg_atomic_int32_t op_completed_count;
    uint32_t op_count;
    bool ack;
    uint8_t flags;
};

/* Operation ID */
struct na_bmi_op_id {
    struct na_cb_completion_data completion_data; /* Completion data */
    union {
        struct na_bmi_msg_info msg;
        struct na_bmi_rma_info rma;
    } info;                          /* Op info                  */
    TAILQ_ENTRY(na_bmi_op_id) entry; /* Entry in queue           */
    na_class_t *na_class;            /* NA class associated      */
    na_context_t *context;           /* NA context associated    */
    struct na_bmi_addr *na_bmi_addr; /* Address associated       */
    hg_atomic_int32_t status;        /* Operation status         */
};

/* Unexpected msg queue */
struct na_bmi_unexpected_msg_queue {
    STAILQ_HEAD(, na_bmi_unexpected_info) queue;
    hg_thread_spin_t lock;
};

/* Op ID queue */
struct na_bmi_op_queue {
    TAILQ_HEAD(, na_bmi_op_id) queue;
    hg_thread_spin_t lock;
};

/* Map (used to cache addresses) */
struct na_bmi_map {
    hg_thread_rwlock_t lock;
    hg_hash_table_t *map;
};

/* Addr queue */
struct na_bmi_addr_queue {
    STAILQ_HEAD(, na_bmi_addr) queue;
    hg_thread_spin_t lock;
};

/* Context */
struct na_bmi_context {
    bmi_context_id context_id;
};

/* Class */
struct na_bmi_class {
    struct na_bmi_unexpected_msg_queue
        unexpected_msg_queue;                   /* Unexpected msg queue */
    struct na_bmi_op_queue unexpected_op_queue; /* Unexpected op queue */
    struct na_bmi_map addr_map;                 /* Address map */
    struct na_bmi_addr_queue addr_queue;        /* Addr queue */
    hg_thread_mutex_t test_unexpected_mutex;    /* Mutex */
    char *protocol_name;                        /* Protocol used */
    char *listen_addr;                          /* Listen addr */
    struct na_bmi_addr *src_addr;               /* Source address */
    size_t unexpected_size_max;                 /* Max unexpected size */
    size_t expected_size_max;                   /* Max expected size */
    int port;                                   /* Port used */
    hg_atomic_int32_t rma_tag;                  /* Atomic RMA tag value */
};

/********************/
/* Local Prototypes */
/********************/

/**
 * Key hash for hash table.
 */
static NA_INLINE unsigned int
na_bmi_addr_key_hash(hg_hash_table_key_t vlocation);

/**
 * Compare key.
 */
static NA_INLINE int
na_bmi_addr_key_equal(
    hg_hash_table_key_t vlocation1, hg_hash_table_key_t vlocation2);

/**
 * Lookup addr key from map.
 */
static NA_INLINE struct na_bmi_addr *
na_bmi_addr_map_lookup(struct na_bmi_map *na_bmi_map, BMI_addr_t bmi_addr);

/**
 * Insert new addr key into map. Execute callback while write lock is acquired.
 */
static na_return_t
na_bmi_addr_map_insert(struct na_bmi_map *na_bmi_map, BMI_addr_t bmi_addr,
    bool unexpected, struct na_bmi_addr_queue *addr_queue,
    struct na_bmi_addr **addr);

/**
 * Remove entry from map.
 */
static void
na_bmi_addr_map_remove(struct na_bmi_map *na_bmi_map,
    struct na_bmi_addr *na_bmi_addr, struct na_bmi_addr_queue *addr_queue);

/**
 * Create new address.
 */
static na_return_t
na_bmi_addr_create(
    BMI_addr_t bmi_addr, bool unexpected, bool self, struct na_bmi_addr **addr);

/**
 * Destroy address.
 */
static void
na_bmi_addr_destroy(struct na_bmi_addr *na_bmi_addr);

/*
 * Generate RMA tags.
 */
static NA_INLINE bmi_msg_tag_t
na_bmi_gen_rma_tag(na_class_t *na_class);

/* progress */
static na_return_t
na_bmi_progress(na_class_t *na_class, na_context_t *context,
    unsigned int timeout, unsigned int *count_p);

/**
 * Progress unexpected messages.
 */
static na_return_t
na_bmi_progress_unexpected(na_class_t *na_class,
    struct na_bmi_class *na_bmi_class, na_context_t *context,
    unsigned int timeout, bool *progressed);

/**
 * Progress expected messages.
 */
static na_return_t
na_bmi_progress_expected(
    na_context_t *context, unsigned int timeout, bool *progressed);

/**
 * Process unexpected messages.
 */
static na_return_t
na_bmi_process_msg_unexpected(struct na_bmi_op_queue *unexpected_op_queue,
    struct na_bmi_addr *na_bmi_addr,
    const struct BMI_unexpected_info *bmi_unexpected_info,
    struct na_bmi_unexpected_msg_queue *unexpected_msg_queue, bool *queued);

/**
 * Process RMA messages.
 */
static na_return_t
na_bmi_process_rma_msg(na_class_t *na_class, na_context_t *context,
    struct na_bmi_addr *na_bmi_addr,
    const struct BMI_unexpected_info *bmi_unexpected_info);

/**
 * Process RMA acks.
 */
static na_return_t
na_bmi_process_rma_ack(struct na_bmi_op_id *na_bmi_op_id, bool *completed);

/**
 * Complete operation.
 */
static void
na_bmi_complete(struct na_bmi_op_id *na_bmi_op_id);

/**
 * Release memory.
 */
static void
na_bmi_release(void *arg);

/* check_protocol */
static bool
na_bmi_check_protocol(const char *protocol_name);

/* initialize */
static na_return_t
na_bmi_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen);

/* finalize */
static na_return_t
na_bmi_finalize(na_class_t *na_class);

/* context_create */
static na_return_t
na_bmi_context_create(na_class_t *na_class, void **context, uint8_t id);

/* context_destroy */
static na_return_t
na_bmi_context_destroy(na_class_t *na_class, void *context);

/* op_create */
static na_op_id_t *
na_bmi_op_create(na_class_t *na_class, unsigned long flags);

/* op_destroy */
static void
na_bmi_op_destroy(na_class_t *na_class, na_op_id_t *op_id);

/* addr_lookup */
static na_return_t
na_bmi_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr);

/* addr_free */
static void
na_bmi_addr_free(na_class_t *na_class, na_addr_t *addr);

/* addr_self */
static na_return_t
na_bmi_addr_self(na_class_t *na_class, na_addr_t **addr);

/* addr_dup */
static na_return_t
na_bmi_addr_dup(na_class_t *na_class, na_addr_t *addr, na_addr_t **new_addr);

/* addr_cmp */
static bool
na_bmi_addr_cmp(na_class_t *na_class, na_addr_t *addr1, na_addr_t *addr2);

/* addr_is_self */
static bool
na_bmi_addr_is_self(na_class_t *na_class, na_addr_t *addr);

/* addr_to_string */
static na_return_t
na_bmi_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr);

/* msg_get_max_unexpected_size */
static size_t
na_bmi_msg_get_max_unexpected_size(const na_class_t *na_class);

/* msg_get_max_expected_size */
static size_t
na_bmi_msg_get_max_expected_size(const na_class_t *na_class);

/* msg_get_max_tag */
static na_tag_t
na_bmi_msg_get_max_tag(const na_class_t *na_class);

/* msg_send_unexpected */
static na_return_t
na_bmi_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_unexpected */
static na_return_t
na_bmi_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id);

/* msg_send_expected */
static na_return_t
na_bmi_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_expected */
static na_return_t
na_bmi_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_addr_t *source_addr, uint8_t source_id, na_tag_t tag, na_op_id_t *op_id);

/* mem_handle_create */
static na_return_t
na_bmi_mem_handle_create(na_class_t *na_class, void *buf, size_t buf_size,
    unsigned long flags, na_mem_handle_t **mem_handle);

/* mem_handle_free */
static void
na_bmi_mem_handle_free(na_class_t *na_class, na_mem_handle_t *mem_handle);

/* mem_handle_get_serialize_size */
static size_t
na_bmi_mem_handle_get_serialize_size(
    na_class_t *na_class, na_mem_handle_t *mem_handle);

/* mem_handle_serialize */
static na_return_t
na_bmi_mem_handle_serialize(na_class_t *na_class, void *buf, size_t buf_size,
    na_mem_handle_t *mem_handle);

/* mem_handle_deserialize */
static na_return_t
na_bmi_mem_handle_deserialize(na_class_t *na_class,
    na_mem_handle_t **mem_handle, const void *buf, size_t buf_size);

/* put */
static na_return_t
na_bmi_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* get */
static na_return_t
na_bmi_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* poll */
static na_return_t
na_bmi_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p);

/* poll_wait */
static na_return_t
na_bmi_poll_wait(na_class_t *na_class, na_context_t *context,
    unsigned int timeout, unsigned int *count_p);

/* cancel */
static na_return_t
na_bmi_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t *op_id);

/*******************/
/* Local Variables */
/*******************/

const struct na_class_ops NA_PLUGIN_OPS(bmi) = {
    "bmi",                                /* name */
    NULL,                                 /* get_protocol_info */
    na_bmi_check_protocol,                /* check_protocol */
    na_bmi_initialize,                    /* initialize */
    na_bmi_finalize,                      /* finalize */
    NULL,                                 /* cleanup */
    NULL,                                 /* has_opt_feature */
    na_bmi_context_create,                /* context_create */
    na_bmi_context_destroy,               /* context_destroy */
    na_bmi_op_create,                     /* op_create */
    na_bmi_op_destroy,                    /* op_destroy */
    na_bmi_addr_lookup,                   /* addr_lookup */
    na_bmi_addr_free,                     /* addr_free */
    NULL,                                 /* addr_set_remove */
    na_bmi_addr_self,                     /* addr_self */
    na_bmi_addr_dup,                      /* addr_dup */
    na_bmi_addr_cmp,                      /* addr_cmp */
    na_bmi_addr_is_self,                  /* addr_is_self */
    na_bmi_addr_to_string,                /* addr_to_string */
    NULL,                                 /* addr_get_serialize_size */
    NULL,                                 /* addr_serialize */
    NULL,                                 /* addr_deserialize */
    na_bmi_msg_get_max_unexpected_size,   /* msg_get_max_unexpected_size */
    na_bmi_msg_get_max_expected_size,     /* msg_get_max_expected_size */
    NULL,                                 /* msg_get_unexpected_header_size */
    NULL,                                 /* msg_get_expected_header_size */
    na_bmi_msg_get_max_tag,               /* msg_get_max_tag */
    NULL,                                 /* msg_buf_alloc */
    NULL,                                 /* msg_buf_free */
    NULL,                                 /* msg_init_unexpected */
    na_bmi_msg_send_unexpected,           /* msg_send_unexpected */
    na_bmi_msg_recv_unexpected,           /* msg_recv_unexpected */
    NULL,                                 /* msg_multi_recv_unexpected */
    NULL,                                 /* msg_init_expected */
    na_bmi_msg_send_expected,             /* msg_send_expected */
    na_bmi_msg_recv_expected,             /* msg_recv_expected */
    na_bmi_mem_handle_create,             /* mem_handle_create */
    NULL,                                 /* mem_handle_create_segment */
    na_bmi_mem_handle_free,               /* mem_handle_free */
    NULL,                                 /* mem_handle_get_max_segments */
    NULL,                                 /* mem_register */
    NULL,                                 /* mem_deregister */
    na_bmi_mem_handle_get_serialize_size, /* mem_handle_get_serialize_size */
    na_bmi_mem_handle_serialize,          /* mem_handle_serialize */
    na_bmi_mem_handle_deserialize,        /* mem_handle_deserialize */
    na_bmi_put,                           /* put */
    na_bmi_get,                           /* get */
    NULL,                                 /* poll_get_fd */
    NULL,                                 /* poll_try_wait */
    na_bmi_poll,                          /* poll */
    na_bmi_poll_wait,                     /* poll_wait */
    na_bmi_cancel                         /* cancel */
};

/********************/
/* Plugin callbacks */
/********************/

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_bmi_addr_key_hash(hg_hash_table_key_t vlocation)
{
    /* Hashing through PIDs should be sufficient in practice */
    return (unsigned int) (*((BMI_addr_t *) vlocation) >> 32);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_bmi_addr_key_equal(
    hg_hash_table_key_t vlocation1, hg_hash_table_key_t vlocation2)
{
    return *((BMI_addr_t *) vlocation1) == *((BMI_addr_t *) vlocation2);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE struct na_bmi_addr *
na_bmi_addr_map_lookup(struct na_bmi_map *na_bmi_map, BMI_addr_t bmi_addr)
{
    struct na_bmi_addr *na_bmi_addr = NULL;

    /* Lookup key */
    hg_thread_rwlock_rdlock(&na_bmi_map->lock);
    na_bmi_addr =
        hg_hash_table_lookup(na_bmi_map->map, (hg_hash_table_key_t) &bmi_addr);
    hg_thread_rwlock_release_rdlock(&na_bmi_map->lock);

    return na_bmi_addr;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_addr_map_insert(struct na_bmi_map *na_bmi_map, BMI_addr_t bmi_addr,
    bool unexpected, struct na_bmi_addr_queue *addr_queue,
    struct na_bmi_addr **addr)
{
    struct na_bmi_addr *na_bmi_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    hg_thread_rwlock_wrlock(&na_bmi_map->lock);

    /* Look up again to prevent race between lock release/acquire */
    na_bmi_addr = (struct na_bmi_addr *) hg_hash_table_lookup(
        na_bmi_map->map, (hg_hash_table_key_t) &bmi_addr);
    if (na_bmi_addr != NULL) {
        ret = NA_EXIST; /* Entry already exists */
        goto done;
    }

    /* Try to pick addr from pool */
    hg_thread_spin_lock(&addr_queue->lock);
    na_bmi_addr = STAILQ_FIRST(&addr_queue->queue);
    if (na_bmi_addr) {
        STAILQ_REMOVE_HEAD(&addr_queue->queue, entry);
        hg_thread_spin_unlock(&addr_queue->lock);
    } else {
        hg_thread_spin_unlock(&addr_queue->lock);
        ret = na_bmi_addr_create(bmi_addr, unexpected, false, &na_bmi_addr);
        NA_CHECK_NA_ERROR(error, ret, "Could not create address");
    }

    na_bmi_addr->bmi_addr = bmi_addr;
    na_bmi_addr->unexpected = unexpected;
    na_bmi_addr->self = false;
    hg_atomic_init32(&na_bmi_addr->ref_count, 1);

    /* Insert new value */
    rc = hg_hash_table_insert(na_bmi_map->map,
        (hg_hash_table_key_t) &na_bmi_addr->bmi_addr,
        (hg_hash_table_value_t) na_bmi_addr);
    NA_CHECK_ERROR(
        rc == 0, error, ret, NA_NOMEM, "hg_hash_table_insert() failed");

done:
    hg_thread_rwlock_release_wrlock(&na_bmi_map->lock);

    *addr = na_bmi_addr;

    return ret;

error:
    hg_thread_rwlock_release_wrlock(&na_bmi_map->lock);

    if (na_bmi_addr) {
        hg_thread_spin_lock(&addr_queue->lock);
        STAILQ_INSERT_TAIL(&addr_queue->queue, na_bmi_addr, entry);
        hg_thread_spin_unlock(&addr_queue->lock);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_bmi_addr_map_remove(struct na_bmi_map *na_bmi_map,
    struct na_bmi_addr *na_bmi_addr, struct na_bmi_addr_queue *addr_queue)
{
    hg_thread_rwlock_wrlock(&na_bmi_map->lock);
    if (hg_hash_table_lookup(
            na_bmi_map->map, (hg_hash_table_key_t) &na_bmi_addr->bmi_addr)) {
        hg_hash_table_remove(
            na_bmi_map->map, (hg_hash_table_key_t) &na_bmi_addr->bmi_addr);

        hg_thread_spin_lock(&addr_queue->lock);
        STAILQ_INSERT_TAIL(&addr_queue->queue, na_bmi_addr, entry);
        hg_thread_spin_unlock(&addr_queue->lock);
    }
    hg_thread_rwlock_release_wrlock(&na_bmi_map->lock);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_addr_create(
    BMI_addr_t bmi_addr, bool unexpected, bool self, struct na_bmi_addr **addr)
{
    struct na_bmi_addr *na_bmi_addr = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Allocate addr */
    na_bmi_addr = (struct na_bmi_addr *) calloc(1, sizeof(*na_bmi_addr));
    NA_CHECK_ERROR(na_bmi_addr == NULL, done, ret, NA_NOMEM,
        "Could not allocate BMI addr");

    na_bmi_addr->bmi_addr = bmi_addr;
    na_bmi_addr->unexpected = unexpected;
    na_bmi_addr->self = self;
    hg_atomic_init32(&na_bmi_addr->ref_count, 1);

    *addr = na_bmi_addr;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_bmi_addr_destroy(struct na_bmi_addr *na_bmi_addr)
{
    free(na_bmi_addr);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bmi_msg_tag_t
na_bmi_gen_rma_tag(na_class_t *na_class)
{
    hg_atomic_cas32(&NA_BMI_CLASS(na_class)->rma_tag, NA_BMI_TAG_MAX, 0);

    /* Increment tag */
    return hg_atomic_incr32(&NA_BMI_CLASS(na_class)->rma_tag);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_progress(na_class_t *na_class, na_context_t *context,
    unsigned int timeout, unsigned int *count_p)
{
    unsigned int count = 0;
    bool progressed = false;
    na_return_t ret;

    /* Try to make progress here from the BMI unexpected queue */
    ret = na_bmi_progress_unexpected(
        na_class, NA_BMI_CLASS(na_class), context, 0, &progressed);
    NA_CHECK_NA_ERROR(error, ret, "Could not make unexpected progress");

    if (progressed)
        count++;

    /* The rule is that the timeout should be passed to testcontext, and
     * that testcontext will return if there is an unexpected message.
     * (And, that as long as there are unexpected messages pending,
     * testcontext will ignore the timeout and immediately return).
     * [verified this in the source] */
    ret = na_bmi_progress_expected(context, timeout, &progressed);
    NA_CHECK_NA_ERROR(error, ret, "Could not make expected progress");

    if (progressed)
        count++;

    if (count_p != NULL)
        *count_p = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_progress_unexpected(na_class_t *na_class,
    struct na_bmi_class *na_bmi_class, na_context_t *context,
    unsigned int timeout, bool *progressed)
{
    int outcount = 0;
    struct BMI_unexpected_info bmi_unexpected_info;
    struct na_bmi_addr *na_bmi_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    bool queued = false;
    int bmi_ret;

    /* Prevent multiple threads from calling BMI_testunexpected concurrently */
    hg_thread_mutex_lock(&na_bmi_class->test_unexpected_mutex);
    bmi_ret =
        BMI_testunexpected(1, &outcount, &bmi_unexpected_info, (int) timeout);
    hg_thread_mutex_unlock(&na_bmi_class->test_unexpected_mutex);
    NA_CHECK_ERROR(bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR,
        "BMI_testunexpected() failed");

    if (!outcount) {
        *progressed = false;
        return ret;
    }

    NA_CHECK_ERROR(bmi_unexpected_info.error_code != 0, cleanup, ret,
        NA_PROTOCOL_ERROR, "BMI_testunexpected failed(), error code set");

    /* Retrieve source addr */
    na_bmi_addr = na_bmi_addr_map_lookup(
        &na_bmi_class->addr_map, bmi_unexpected_info.addr);
    if (!na_bmi_addr) {
        na_return_t na_ret;

        NA_LOG_DEBUG("Address was not found, attempting to insert it (key=%ld)",
            (long int) bmi_unexpected_info.addr);

        /* Insert new entry and create new address if needed */
        na_ret = na_bmi_addr_map_insert(&na_bmi_class->addr_map,
            bmi_unexpected_info.addr, true, &na_bmi_class->addr_queue,
            &na_bmi_addr);
        NA_CHECK_ERROR(na_ret != NA_SUCCESS && na_ret != NA_EXIST, done, ret,
            na_ret, "Could not insert new address");
    } else
        NA_LOG_DEBUG(
            "Address was found (key=%ld)", (long int) bmi_unexpected_info.addr);

    /* Unexpected RMA msg for RMA emulation */
    if (bmi_unexpected_info.tag & NA_BMI_RMA_MSG_TAG) {
        /* Make RMA progress */
        ret = na_bmi_process_rma_msg(
            na_class, context, na_bmi_addr, &bmi_unexpected_info);
        NA_CHECK_NA_ERROR(cleanup, ret, "Could not make RMA progress");
    } else {
        ret = na_bmi_process_msg_unexpected(&na_bmi_class->unexpected_op_queue,
            na_bmi_addr, &bmi_unexpected_info,
            &na_bmi_class->unexpected_msg_queue, &queued);
        NA_CHECK_NA_ERROR(cleanup, ret, "Could not process unexpected msg");
    }

    *progressed = true;

cleanup:
    if (!queued)
        BMI_unexpected_free(
            bmi_unexpected_info.addr, bmi_unexpected_info.buffer);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_progress_expected(
    na_context_t *context, unsigned int timeout, bool *progressed)
{
    bmi_op_id_t bmi_op_id = 0;
    int outcount = 0;
    bmi_error_code_t error_code = 0;
    bmi_size_t bmi_actual_size = 0;
    struct na_bmi_op_id *na_bmi_op_id = NULL;
    na_return_t ret = NA_SUCCESS;
    int bmi_ret = 0;

    /* Return as soon as something completes or timeout is reached */
    bmi_ret = BMI_testcontext(1, &bmi_op_id, &outcount, &error_code,
        &bmi_actual_size, (void **) &na_bmi_op_id, (int) timeout,
        NA_BMI_CONTEXT(context)->context_id);
    NA_CHECK_ERROR(bmi_ret < 0 && (error_code != 0), done, ret,
        NA_PROTOCOL_ERROR, "BMI_testcontext() failed");

    if (!outcount) {
        *progressed = false;
        return ret;
    }

    if (error_code == -BMI_ECANCEL) {
        NA_CHECK_ERROR(
            hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED, done,
            ret, NA_FAULT, "Operation ID was completed");
        NA_LOG_DEBUG(
            "BMI_ECANCEL event on operation ID %p", (void *) na_bmi_op_id);
        NA_CHECK_ERROR(
            !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_CANCELED),
            done, ret, NA_FAULT, "Operation ID was not canceled");
    } else
        NA_CHECK_ERROR(error_code != 0, done, ret, NA_PROTOCOL_ERROR,
            "BMI_testcontext() failed, error code set");

    switch (na_bmi_op_id->completion_data.callback_info.type) {
        case NA_CB_RECV_EXPECTED:
            /* Set the actual size */
            na_bmi_op_id->info.msg.actual_buf_size = bmi_actual_size;
            NA_FALLTHROUGH;
        case NA_CB_SEND_UNEXPECTED:
        case NA_CB_SEND_EXPECTED:
            na_bmi_complete(na_bmi_op_id);
            break;
        case NA_CB_PUT:
        case NA_CB_GET:
            if (bmi_op_id == na_bmi_op_id->info.rma.msg_op_id) {
                /* Nothing */
            } else if (bmi_op_id == na_bmi_op_id->info.rma.rma_op_id) {
                /* Process ack if requested */
                if ((na_bmi_op_id->info.rma.flags & NA_BMI_RMA_SVC) &&
                    (na_bmi_op_id->info.rma.flags & NA_BMI_RMA_ACK)) {
                    bool ack_completed;

                    ret = na_bmi_process_rma_ack(na_bmi_op_id, &ack_completed);
                    NA_CHECK_NA_ERROR(done, ret, "Could not process ack");

                    if (ack_completed)
                        hg_atomic_incr32(
                            &na_bmi_op_id->info.rma.op_completed_count);
                }
            } else if (bmi_op_id == na_bmi_op_id->info.rma.ack_op_id) {
                /* Check ack completion flag */
                if (!(na_bmi_op_id->info.rma.flags & NA_BMI_RMA_SVC) &&
                    (error_code == 0)) {
                    NA_CHECK_ERROR(!na_bmi_op_id->info.rma.ack, done, ret,
                        NA_PROTOCOL_ERROR,
                        "Error during transfer, ack received is %u",
                        na_bmi_op_id->info.rma.ack);
                }
            } else
                NA_GOTO_ERROR(done, ret, NA_FAULT, "Unexpected operation ID");

            /* Complete op ID when reached expected completion count */
            if (hg_atomic_incr32(&na_bmi_op_id->info.rma.op_completed_count) ==
                (int32_t) na_bmi_op_id->info.rma.op_count)
                na_bmi_complete(na_bmi_op_id);
            break;
        case NA_CB_RECV_UNEXPECTED:
            NA_GOTO_ERROR(done, ret, NA_FAULT,
                "Should not complete unexpected recv here");
            break;
        default:
            NA_GOTO_ERROR(
                done, ret, NA_PROTOCOL_ERROR, "Unknown type of operation ID");
    }

    *progressed = true;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_process_msg_unexpected(struct na_bmi_op_queue *unexpected_op_queue,
    struct na_bmi_addr *na_bmi_addr,
    const struct BMI_unexpected_info *bmi_unexpected_info,
    struct na_bmi_unexpected_msg_queue *unexpected_msg_queue, bool *queued)
{
    struct na_bmi_op_id *na_bmi_op_id = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Pop op ID from queue */
    hg_thread_spin_lock(&unexpected_op_queue->lock);
    na_bmi_op_id = TAILQ_FIRST(&unexpected_op_queue->queue);
    if (likely(na_bmi_op_id)) {
        TAILQ_REMOVE(&unexpected_op_queue->queue, na_bmi_op_id, entry);
        hg_atomic_and32(&na_bmi_op_id->status, ~NA_BMI_OP_QUEUED);
    }
    hg_thread_spin_unlock(&unexpected_op_queue->lock);

    if (likely(na_bmi_op_id)) {
        /* Fill info */
        na_bmi_op_id->na_bmi_addr = na_bmi_addr;
        hg_atomic_incr32(&na_bmi_addr->ref_count);
        na_bmi_op_id->info.msg.actual_buf_size = bmi_unexpected_info->size;
        na_bmi_op_id->info.msg.tag = bmi_unexpected_info->tag;

        /* Copy buffer */
        memcpy(na_bmi_op_id->info.msg.buf.ptr, bmi_unexpected_info->buffer,
            (size_t) bmi_unexpected_info->size);

        na_bmi_complete(na_bmi_op_id);

        *queued = false;
    } else {
        struct na_bmi_unexpected_info *na_bmi_unexpected_info = NULL;

        /* If no error and message arrived, keep a copy of the struct in
         * the unexpected message queue */
        na_bmi_unexpected_info = (struct na_bmi_unexpected_info *) malloc(
            sizeof(struct na_bmi_unexpected_info));
        NA_CHECK_ERROR(na_bmi_unexpected_info == NULL, done, ret, NA_NOMEM,
            "Could not allocate unexpected info");

        na_bmi_unexpected_info->na_bmi_addr = na_bmi_addr;
        hg_atomic_incr32(&na_bmi_addr->ref_count);

        memcpy(&na_bmi_unexpected_info->info, bmi_unexpected_info,
            sizeof(struct BMI_unexpected_info));

        /* Otherwise push the unexpected message into our
         * unexpected queue so that we can treat it later when a
         * recv_unexpected is posted */
        hg_thread_spin_lock(&unexpected_msg_queue->lock);
        STAILQ_INSERT_TAIL(
            &unexpected_msg_queue->queue, na_bmi_unexpected_info, entry);
        hg_thread_spin_unlock(&unexpected_msg_queue->lock);

        *queued = true;
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_process_rma_msg(na_class_t *na_class, na_context_t *context,
    struct na_bmi_addr *na_bmi_addr,
    const struct BMI_unexpected_info *bmi_unexpected_info)
{
    struct na_bmi_op_id *na_bmi_op_id = NULL;
    na_return_t ret = NA_SUCCESS;
    int bmi_ret;

    NA_CHECK_ERROR(
        bmi_unexpected_info->size != sizeof(struct na_bmi_rma_msg_info), error,
        ret, NA_FAULT,
        "Unexpected message size does not match RMA info struct");

    /* Allocate na_op_id */
    na_bmi_op_id = (struct na_bmi_op_id *) na_bmi_op_create(na_class, 0);
    NA_CHECK_ERROR(na_bmi_op_id == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA BMI operation ID");

    /* Check op_id */
    NA_CHECK_ERROR(na_bmi_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_ERROR(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed");

    /* This is an internal operation so no user callback/arg */
    na_bmi_op_id->context = context;
    na_bmi_op_id->completion_data.callback_info.type = 0;
    na_bmi_op_id->completion_data.callback = NULL;
    na_bmi_op_id->completion_data.callback_info.arg = NULL;
    hg_atomic_incr32(&na_bmi_addr->ref_count);
    na_bmi_op_id->na_bmi_addr = na_bmi_addr;
    hg_atomic_set32(&na_bmi_op_id->status, 0);

    /* Mark op ID as RMA svc */
    na_bmi_op_id->info.rma.flags = NA_BMI_RMA_SVC;

    memcpy(&na_bmi_op_id->info.rma.msg_info, bmi_unexpected_info->buffer,
        (size_t) bmi_unexpected_info->size);

    switch (na_bmi_op_id->info.rma.msg_info.op) {
        /* Remote wants to do a put so wait in a recv */
        case NA_BMI_RMA_PUT:
            na_bmi_op_id->completion_data.callback_info.type = NA_CB_PUT;
            na_bmi_op_id->info.rma.base =
                (void *) na_bmi_op_id->info.rma.msg_info.base;
            na_bmi_op_id->info.rma.actual_len = 0;
            na_bmi_op_id->info.rma.msg_op_id = 0;
            na_bmi_op_id->info.rma.rma_op_id = 0;
            na_bmi_op_id->info.rma.ack_op_id = 0;
            na_bmi_op_id->info.rma.ack = false;
            na_bmi_op_id->info.rma.ack_size = 0;
            na_bmi_op_id->info.rma.flags |= NA_BMI_RMA_ACK;
            na_bmi_op_id->info.rma.op_count = 2;
            hg_atomic_init32(&na_bmi_op_id->info.rma.op_completed_count, 0);

            /* Start receiving data */
            bmi_ret = BMI_post_recv(&na_bmi_op_id->info.rma.rma_op_id,
                na_bmi_addr->bmi_addr, na_bmi_op_id->info.rma.base,
                na_bmi_op_id->info.rma.msg_info.len,
                &na_bmi_op_id->info.rma.actual_len, BMI_EXT_ALLOC,
                na_bmi_op_id->info.rma.msg_info.rma_tag, na_bmi_op_id,
                NA_BMI_CONTEXT(context)->context_id, NULL);
            NA_CHECK_ERROR(bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR,
                "BMI_post_recv() failed");

            /* Immediate completion */
            if (bmi_ret > 0) {
                bool ack_completed;

                hg_atomic_incr32(&na_bmi_op_id->info.rma.op_completed_count);

                /* Process ack directly */
                ret = na_bmi_process_rma_ack(na_bmi_op_id, &ack_completed);
                NA_CHECK_NA_ERROR(error, ret, "Could not process ack");

                if (ack_completed) {
                    hg_atomic_incr32(
                        &na_bmi_op_id->info.rma.op_completed_count);
                    na_bmi_complete(na_bmi_op_id);
                }
            }
            break;
            /* Remote wants to do a get so do a send */
        case NA_BMI_RMA_GET:
            na_bmi_op_id->completion_data.callback_info.type = NA_CB_GET;
            na_bmi_op_id->info.rma.base =
                (void *) na_bmi_op_id->info.rma.msg_info.base;
            na_bmi_op_id->info.rma.actual_len = 0;
            na_bmi_op_id->info.rma.msg_op_id = 0;
            na_bmi_op_id->info.rma.rma_op_id = 0;
            na_bmi_op_id->info.rma.ack_op_id = 0;
            na_bmi_op_id->info.rma.ack = false;
            na_bmi_op_id->info.rma.ack_size = 0;
            na_bmi_op_id->info.rma.flags |= 0;
            na_bmi_op_id->info.rma.op_count = 1;
            hg_atomic_init32(&na_bmi_op_id->info.rma.op_completed_count, 0);

            /* Start sending data */
            bmi_ret = BMI_post_send(&na_bmi_op_id->info.rma.rma_op_id,
                na_bmi_addr->bmi_addr, na_bmi_op_id->info.rma.base,
                na_bmi_op_id->info.rma.msg_info.len, BMI_EXT_ALLOC,
                na_bmi_op_id->info.rma.msg_info.rma_tag, na_bmi_op_id,
                NA_BMI_CONTEXT(context)->context_id, NULL);
            NA_CHECK_ERROR(bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR,
                "BMI_post_send() failed");

            if (bmi_ret > 0) {
                hg_atomic_incr32(&na_bmi_op_id->info.rma.op_completed_count);
                na_bmi_complete(na_bmi_op_id);
            }
            break;
        default:
            NA_GOTO_ERROR(error, ret, NA_FAULT, "Operation not supported");
    }

    return ret;

error:
    if (na_bmi_op_id)
        na_bmi_op_destroy(na_class, (na_op_id_t *) na_bmi_op_id);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_process_rma_ack(struct na_bmi_op_id *na_bmi_op_id, bool *completed)
{
    na_return_t ret = NA_SUCCESS;
    int bmi_ret;

    na_bmi_op_id->info.rma.ack = true;

    /* Send an ack to tell the server that the data is here */
    bmi_ret = BMI_post_send(&na_bmi_op_id->info.rma.ack_op_id,
        na_bmi_op_id->na_bmi_addr->bmi_addr, &na_bmi_op_id->info.rma.ack,
        sizeof(bool), BMI_EXT_ALLOC, na_bmi_op_id->info.rma.msg_info.ack_tag,
        na_bmi_op_id, NA_BMI_CONTEXT(na_bmi_op_id->context)->context_id, NULL);
    NA_CHECK_ERROR(
        bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR, "BMI_post_send() failed");

    *completed = (bmi_ret > 0) ? true : false;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_bmi_complete(struct na_bmi_op_id *na_bmi_op_id)
{
    struct na_cb_info *callback_info = NULL;
    int32_t status;
    bool op_internal = false;

    /* Mark op id as completed before checking for cancelation */
    status = hg_atomic_or32(&na_bmi_op_id->status, NA_BMI_OP_COMPLETED);

    /* Init callback info */
    callback_info = &na_bmi_op_id->completion_data.callback_info;

    /* Check for current status before completing */
    if (status & NA_BMI_OP_CANCELED) {
        /* If it was canceled while being processed, set callback ret
         * accordingly */
        NA_LOG_DEBUG("Operation ID %p was canceled", (void *) na_bmi_op_id);
        callback_info->ret = NA_CANCELED;
    } else
        callback_info->ret = NA_SUCCESS;

    switch (callback_info->type) {
        case NA_CB_RECV_UNEXPECTED:
            if (callback_info->ret != NA_SUCCESS) {
                /* In case of cancellation where no recv'd data */
                callback_info->info.recv_unexpected.actual_buf_size = 0;
                callback_info->info.recv_unexpected.source = NULL;
                callback_info->info.recv_unexpected.tag = 0;
            } else {
                /* Increment addr ref count */
                hg_atomic_incr32(&na_bmi_op_id->na_bmi_addr->ref_count);

                /* Fill callback info */
                callback_info->info.recv_unexpected.actual_buf_size =
                    (size_t) na_bmi_op_id->info.msg.actual_buf_size;
                callback_info->info.recv_unexpected.source =
                    (na_addr_t *) na_bmi_op_id->na_bmi_addr;
                callback_info->info.recv_unexpected.tag =
                    (na_tag_t) na_bmi_op_id->info.msg.tag;
            }
            break;
        case NA_CB_SEND_UNEXPECTED:
        case NA_CB_SEND_EXPECTED:
            break;
        case NA_CB_RECV_EXPECTED:
            if (callback_info->ret != NA_SUCCESS)
                callback_info->info.recv_expected.actual_buf_size = 0;
            else
                callback_info->info.recv_expected.actual_buf_size =
                    (size_t) na_bmi_op_id->info.msg.actual_buf_size;
            break;
        case NA_CB_PUT:
        case NA_CB_GET:
            if (na_bmi_op_id->info.rma.flags & NA_BMI_RMA_SVC)
                op_internal = true;
            break;
        default:
            NA_LOG_ERROR(
                "Operation type %d not supported", callback_info->type);
            break;
    }

    /* Add OP to NA completion queue */
    if (op_internal)
        na_bmi_op_destroy(na_bmi_op_id->na_class, (na_op_id_t *) na_bmi_op_id);
    else
        na_cb_completion_add(
            na_bmi_op_id->context, &na_bmi_op_id->completion_data);
}

/*---------------------------------------------------------------------------*/
static void
na_bmi_release(void *arg)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) arg;

    NA_CHECK_WARNING(na_bmi_op_id && (!(hg_atomic_get32(&na_bmi_op_id->status) &
                                         NA_BMI_OP_COMPLETED)),
        "Releasing resources from an uncompleted operation");

    if (na_bmi_op_id->na_bmi_addr) {
        na_bmi_addr_free(
            na_bmi_op_id->na_class, (na_addr_t *) na_bmi_op_id->na_bmi_addr);
        na_bmi_op_id->na_bmi_addr = NULL;
    }
}

/*---------------------------------------------------------------------------*/
static bool
na_bmi_check_protocol(const char *protocol_name)
{
    bool accept = false;

    /* Note: BMI_SUPPORTS_TRANSPORT_METHOD_GETINFO is not defined
     *       anywhere.  This is a temporary way to disable this fully
     *       functional code to avoid incompatibility with older versions
     *       of BMI.  We will remove this #ifdef to always use the
     *       BMI_get_info API and find out the protocols supported by
     *       the BMI library.
     */
#ifdef BMI_SUPPORTS_TRANSPORT_METHOD_GETINFO
    int string_length = 0;
    char *transport = NULL;
    char *transport_index = NULL;

    /* Obtain the list of transport protocols supported by BMI. */
    string_length = BMI_get_info(0, BMI_TRANSPORT_METHODS_STRING, &transport);

    if (string_length <= 0 || transport == NULL) {
        /* bmi is not configured with any plugins, transport is NULL */
        return false;
    }

    transport_index = strtok(transport, ",");

    while (transport_index != NULL) {
        /* check if bmi supports the protocol. */
        if (strcmp(transport_index, protocol_name) == 0) {
            accept = true;
            break;
        }

        transport_index = strtok(NULL, ",");
    }

    free(transport);
#else
    if ((strcmp(protocol_name, "tcp") == 0) ||
        (strcmp(protocol_name, "ib") == 0)) {
        accept = true;
    }
#endif

    return accept;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen)
{
    const struct na_init_info *na_init_info = &na_info->na_init_info;
    struct na_bmi_class *na_bmi_class = NULL;
    char method_list[NA_BMI_ADDR_NAME_MAX] = {'\0'},
         listen_addr[NA_BMI_ADDR_NAME_MAX] = {'\0'},
         my_hostname[NA_BMI_ADDR_NAME_MAX] = {'\0'},
         pref_anyip[16] = {'\0'}; /* for INADDR_ANY */
    char *method_list_p = NULL, *listen_addr_p = NULL;
    int flag = (listen) ? (BMI_INIT_SERVER | BMI_TCP_BIND_SPECIFIC) : 0,
        port = 0;
    na_return_t ret;
    int bmi_ret, i;
    bool anyaddr = false;

    /* Allocate private data */
    na_bmi_class = (struct na_bmi_class *) calloc(1, sizeof(*na_bmi_class));
    NA_CHECK_ERROR(na_bmi_class == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA private data class");

    STAILQ_INIT(&na_bmi_class->unexpected_msg_queue.queue);
    hg_thread_spin_init(&na_bmi_class->unexpected_msg_queue.lock);

    TAILQ_INIT(&na_bmi_class->unexpected_op_queue.queue);
    hg_thread_spin_init(&na_bmi_class->unexpected_op_queue.lock);

    STAILQ_INIT(&na_bmi_class->addr_queue.queue);
    hg_thread_spin_init(&na_bmi_class->addr_queue.lock);

    /* Initialize mutex/cond */
    hg_thread_mutex_init(&na_bmi_class->test_unexpected_mutex);

    /* Set msg size limits */
    na_bmi_class->unexpected_size_max = na_init_info->max_unexpected_size
                                            ? na_init_info->max_unexpected_size
                                            : NA_BMI_UNEXPECTED_SIZE;
    na_bmi_class->expected_size_max = na_init_info->max_expected_size
                                          ? na_init_info->max_expected_size
                                          : NA_BMI_EXPECTED_SIZE;

    na_bmi_class->protocol_name = strdup(na_info->protocol_name);
    NA_CHECK_ERROR(na_bmi_class->protocol_name == NULL, error, ret, NA_NOMEM,
        "Could not dup protocol name");

    /* Preallocate addresses */
    for (i = 0; i < NA_BMI_ADDR_PREALLOC; i++) {
        struct na_bmi_addr *na_bmi_addr = NULL;

        ret = na_bmi_addr_create(0, false, false, &na_bmi_addr);
        NA_CHECK_NA_ERROR(error, ret, "Could not create address");

        STAILQ_INSERT_TAIL(&na_bmi_class->addr_queue.queue, na_bmi_addr, entry);
    }

    /* Create addr hash-table */
    na_bmi_class->addr_map.map =
        hg_hash_table_new(na_bmi_addr_key_hash, na_bmi_addr_key_equal);
    NA_CHECK_ERROR(na_bmi_class->addr_map.map == NULL, error, ret, NA_NOMEM,
        "hg_hash_table_new() failed");
    hg_hash_table_register_free_functions(
        na_bmi_class->addr_map.map, NULL, NULL);
    hg_thread_rwlock_init(&na_bmi_class->addr_map.lock);

    /* Keep self address */
    ret = na_bmi_addr_create(0, false, true, &na_bmi_class->src_addr);
    NA_CHECK_NA_ERROR(error, ret, "Could not create src address");

    if (listen) {
        int desc_len = 0;

        /* Set pointers to pass to BMI_initialize */
        method_list_p = method_list;
        listen_addr_p = listen_addr;

        /* Method list */
        strcpy(method_list, "bmi_");
        strncat(method_list, na_info->protocol_name,
            NA_BMI_ADDR_NAME_MAX - strlen(method_list));

        if (na_info->host_name) {
            /* Extract hostname */
            strncpy(my_hostname, na_info->host_name, NA_BMI_ADDR_NAME_MAX - 1);
            if (strstr(my_hostname, ":")) {
                char *port_str;

                strtok_r(my_hostname, ":", &port_str);
                port = atoi(port_str);
            }
        } else {
            /* Addr unspecified but we are in server mode; use INADDR_ANY
             * and let BMI choose port.
             */
            snprintf(my_hostname, sizeof(my_hostname), "0.0.0.0");
        }

        /* get pref IP addr by subnet for INADDR_ANY */
        if (strcmp(my_hostname, "0.0.0.0") == 0) {
            uint32_t subnet = 0, netmask = 0;

            if (na_init_info->ip_subnet) {
                ret = na_ip_parse_subnet(
                    na_init_info->ip_subnet, &subnet, &netmask);
                NA_CHECK_NA_ERROR(
                    error, ret, "BMI_initialize() failed - NA_Parse_subnet");
            }
            ret = na_ip_pref_addr(subnet, netmask, pref_anyip);
            NA_CHECK_NA_ERROR(
                error, ret, "BMI_initialize() failed - NA_Pref_ipaddr");
            anyaddr = true;
        }

        /* Pick a default port */
        if (!port)
            desc_len = snprintf(listen_addr, NA_BMI_ADDR_NAME_MAX, "%s://%s",
                na_info->protocol_name, my_hostname);
        else
            desc_len = snprintf(listen_addr, NA_BMI_ADDR_NAME_MAX, "%s://%s:%d",
                na_info->protocol_name, my_hostname, port);

        NA_CHECK_ERROR(desc_len > NA_BMI_ADDR_NAME_MAX, error, ret, NA_OVERFLOW,
            "Exceeding max addr name");
    }

    /* Initialize BMI */
    bmi_ret = BMI_initialize(method_list_p, listen_addr_p, flag);
    NA_CHECK_ERROR(
        bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR, "BMI_initialize() failed");

    /* Resolve listen info that will be used for self address */
    if (listen) {
        int desc_len = 0;

        if (port <= 0) {
            /* if port was not specified, then we need to query BMI */
            BMI_get_info(0, BMI_TCP_GET_PORT, &port);
        }

        desc_len = snprintf(listen_addr, NA_BMI_ADDR_NAME_MAX, "%s://%s:%d",
            na_info->protocol_name, anyaddr ? pref_anyip : my_hostname, port);
        NA_CHECK_ERROR(desc_len > NA_BMI_ADDR_NAME_MAX, error, ret, NA_OVERFLOW,
            "Exceeding max addr name");

        /* Resolve src addr */
        bmi_ret =
            BMI_addr_lookup(&na_bmi_class->src_addr->bmi_addr, listen_addr);
        NA_CHECK_ERROR(bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR,
            "BMI_addr_lookup() failed");

        /* Keep listen_addr and port */
        na_bmi_class->listen_addr = strdup(listen_addr_p);
        NA_CHECK_ERROR(na_bmi_class->listen_addr == NULL, error, ret, NA_NOMEM,
            "Could not dup listen addr");
        na_bmi_class->port = port;
    }

    /* Initialize atomic op */
    hg_atomic_set32(&na_bmi_class->rma_tag, NA_BMI_RMA_TAG);

    na_class->plugin_class = (void *) na_bmi_class;

    return NA_SUCCESS;

error:
    if (na_class->plugin_class) {
        free(na_bmi_class->protocol_name);
        free(na_bmi_class->listen_addr);

        hg_thread_spin_destroy(&na_bmi_class->unexpected_msg_queue.lock);
        hg_thread_spin_destroy(&na_bmi_class->unexpected_op_queue.lock);
        hg_thread_spin_destroy(&na_bmi_class->addr_queue.lock);
        hg_thread_mutex_destroy(&na_bmi_class->test_unexpected_mutex);

        /* Check that addr queue is empty */
        while (!STAILQ_EMPTY(&na_bmi_class->addr_queue.queue)) {
            struct na_bmi_addr *na_bmi_addr =
                STAILQ_FIRST(&na_bmi_class->addr_queue.queue);
            STAILQ_REMOVE_HEAD(&na_bmi_class->addr_queue.queue, entry);
            na_bmi_addr_destroy(na_bmi_addr);
        }

        if (na_bmi_class->addr_map.map) {
            hg_hash_table_free(na_bmi_class->addr_map.map);
            hg_thread_rwlock_destroy(&na_bmi_class->addr_map.lock);
        }

        na_bmi_addr_destroy(na_bmi_class->src_addr);
        free(na_class->plugin_class);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_finalize(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;
    bool empty;
    int bmi_ret;

    if (!na_class->plugin_class)
        goto done;

    /* Check that unexpected op queue is empty */
    empty = TAILQ_EMPTY(&NA_BMI_CLASS(na_class)->unexpected_op_queue.queue);
    NA_CHECK_ERROR(empty == false, done, ret, NA_BUSY,
        "Unexpected op queue should be empty");

    /* Check that unexpected message queue is empty */
    empty = STAILQ_EMPTY(&NA_BMI_CLASS(na_class)->unexpected_msg_queue.queue);
    NA_CHECK_ERROR(empty == false, done, ret, NA_BUSY,
        "Unexpected msg queue should be empty");

    /* Check that addr queue is empty */
    while (!STAILQ_EMPTY(&NA_BMI_CLASS(na_class)->addr_queue.queue)) {
        struct na_bmi_addr *na_bmi_addr =
            STAILQ_FIRST(&NA_BMI_CLASS(na_class)->addr_queue.queue);
        STAILQ_REMOVE_HEAD(&NA_BMI_CLASS(na_class)->addr_queue.queue, entry);
        na_bmi_addr_destroy(na_bmi_addr);
    }

    /* Finalize BMI */
    bmi_ret = BMI_finalize();
    NA_CHECK_ERROR(
        bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR, "BMI_finalize() failed");

    /* Free hash table */
    if (NA_BMI_CLASS(na_class)->addr_map.map) {
        hg_hash_table_iter_t iter;

        hg_hash_table_iterate(NA_BMI_CLASS(na_class)->addr_map.map, &iter);

        while (hg_hash_table_iter_has_more(&iter)) {
            na_bmi_addr_destroy(hg_hash_table_iter_next(&iter));
        }

        hg_hash_table_free(NA_BMI_CLASS(na_class)->addr_map.map);
        hg_thread_rwlock_destroy(&NA_BMI_CLASS(na_class)->addr_map.lock);
    }

    /* Destroy src addr */
    na_bmi_addr_destroy(NA_BMI_CLASS(na_class)->src_addr);

    /* Destroy mutex/cond */
    hg_thread_spin_destroy(&NA_BMI_CLASS(na_class)->addr_queue.lock);
    hg_thread_mutex_destroy(&NA_BMI_CLASS(na_class)->test_unexpected_mutex);
    hg_thread_spin_destroy(&NA_BMI_CLASS(na_class)->unexpected_msg_queue.lock);
    hg_thread_spin_destroy(&NA_BMI_CLASS(na_class)->unexpected_op_queue.lock);

    free(NA_BMI_CLASS(na_class)->listen_addr);
    free(NA_BMI_CLASS(na_class)->protocol_name);
    free(na_class->plugin_class);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_context_create(
    na_class_t NA_UNUSED *na_class, void **context, uint8_t NA_UNUSED id)
{
    struct na_bmi_context *na_bmi_context = NULL;
    na_return_t ret = NA_SUCCESS;
    int bmi_ret;

    na_bmi_context =
        (struct na_bmi_context *) malloc(sizeof(struct na_bmi_context));
    NA_CHECK_ERROR(na_bmi_context == NULL, error, ret, NA_NOMEM,
        "Could not allocate BMI private context");

    /* Create a new BMI context */
    bmi_ret = BMI_open_context(&na_bmi_context->context_id);
    NA_CHECK_ERROR(bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR,
        "BMI_open_context() failed");

    *context = na_bmi_context;

    return ret;

error:
    free(na_bmi_context);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_context_destroy(na_class_t NA_UNUSED *na_class, void *context)
{
    struct na_bmi_context *na_bmi_context = (struct na_bmi_context *) context;

    /* Close BMI context */
    BMI_close_context(na_bmi_context->context_id);

    free(na_bmi_context);

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_op_id_t *
na_bmi_op_create(na_class_t *na_class, unsigned long NA_UNUSED flags)
{
    struct na_bmi_op_id *na_bmi_op_id = NULL;

    na_bmi_op_id = (struct na_bmi_op_id *) malloc(sizeof(struct na_bmi_op_id));
    NA_CHECK_ERROR_NORET(
        na_bmi_op_id == NULL, done, "Could not allocate NA BMI operation ID");
    memset(na_bmi_op_id, 0, sizeof(struct na_bmi_op_id));

    na_bmi_op_id->na_class = na_class;

    /* Completed by default */
    hg_atomic_init32(&na_bmi_op_id->status, NA_BMI_OP_COMPLETED);

    /* Set op ID release callbacks */
    na_bmi_op_id->completion_data.plugin_callback = na_bmi_release;
    na_bmi_op_id->completion_data.plugin_callback_args = na_bmi_op_id;

done:
    return (na_op_id_t *) na_bmi_op_id;
}

/*---------------------------------------------------------------------------*/
static void
na_bmi_op_destroy(na_class_t NA_UNUSED *na_class, na_op_id_t *op_id)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;

    NA_CHECK_WARNING(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED),
        "Attempting to free OP ID that was not completed");

    free(na_bmi_op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr)
{
    struct na_bmi_addr *na_bmi_addr = NULL;
    BMI_addr_t bmi_addr;
    na_return_t ret = NA_SUCCESS;
    int bmi_ret;

    /* Perform an address lookup */
    bmi_ret = BMI_addr_lookup(&bmi_addr, name);
    NA_CHECK_ERROR(
        bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR, "BMI_addr_lookup() failed");

    /* Retrieve target addr */
    na_bmi_addr =
        na_bmi_addr_map_lookup(&NA_BMI_CLASS(na_class)->addr_map, bmi_addr);
    if (!na_bmi_addr) {
        na_return_t na_ret;

        NA_LOG_DEBUG("Address was not found, attempting to insert it (key=%ld)",
            (long int) bmi_addr);

        /* Insert new entry and create new address if needed */
        na_ret = na_bmi_addr_map_insert(&NA_BMI_CLASS(na_class)->addr_map,
            bmi_addr, false, &NA_BMI_CLASS(na_class)->addr_queue, &na_bmi_addr);
        NA_CHECK_ERROR(na_ret != NA_SUCCESS && na_ret != NA_EXIST, done, ret,
            na_ret, "Could not insert new address");
    } else
        NA_LOG_DEBUG("Address was found (key=%ld)", (long int) bmi_addr);

    *addr = (na_addr_t *) na_bmi_addr;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_addr_self(na_class_t *na_class, na_addr_t **addr)
{
    struct na_bmi_addr *na_bmi_addr = NA_BMI_CLASS(na_class)->src_addr;

    /* Increment refcount */
    hg_atomic_incr32(&na_bmi_addr->ref_count);

    *addr = (na_addr_t *) na_bmi_addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_addr_dup(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr, na_addr_t **new_addr)
{
    struct na_bmi_addr *na_bmi_addr = (struct na_bmi_addr *) addr;

    /* Increment refcount */
    hg_atomic_incr32(&na_bmi_addr->ref_count);

    *new_addr = (na_addr_t *) na_bmi_addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static void
na_bmi_addr_free(na_class_t *na_class, na_addr_t *addr)
{
    struct na_bmi_addr *na_bmi_addr = (struct na_bmi_addr *) addr;

    /* Cleanup peer_addr */
    if (na_bmi_addr == NULL)
        return;

    if (hg_atomic_decr32(&na_bmi_addr->ref_count))
        /* Cannot free yet */
        return;

    /* Remove from hash table */
    na_bmi_addr_map_remove(&NA_BMI_CLASS(na_class)->addr_map, na_bmi_addr,
        &NA_BMI_CLASS(na_class)->addr_queue);
}

/*---------------------------------------------------------------------------*/
static bool
na_bmi_addr_cmp(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr1, na_addr_t *addr2)
{
    return (((struct na_bmi_addr *) addr1)->bmi_addr ==
            ((struct na_bmi_addr *) addr2)->bmi_addr);
}

/*---------------------------------------------------------------------------*/
static bool
na_bmi_addr_is_self(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    return ((struct na_bmi_addr *) addr)->self;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr)
{
    struct na_bmi_addr *na_bmi_addr = NULL;
    char full_rev_addr[NA_BMI_ADDR_NAME_MAX + 3] = {'\0'};
    const char *bmi_rev_addr;
    size_t string_len;
    na_return_t ret = NA_SUCCESS;

    na_bmi_addr = (struct na_bmi_addr *) addr;

    if (na_bmi_addr->self) {
        bmi_rev_addr = NA_BMI_CLASS(na_class)->listen_addr;
        NA_CHECK_ERROR(bmi_rev_addr == NULL, done, ret, NA_OPNOTSUPPORTED,
            "Cannot convert addr to string if not listening");
    } else {
        if (na_bmi_addr->unexpected) {
            int desc_len = 0;

            bmi_rev_addr =
                BMI_addr_rev_lookup_unexpected(na_bmi_addr->bmi_addr);

            /* Work around address returned in different format */
            desc_len = snprintf(full_rev_addr, NA_BMI_ADDR_NAME_MAX,
                "%s://%s:%d", NA_BMI_CLASS(na_class)->protocol_name,
                bmi_rev_addr, NA_BMI_CLASS(na_class)->port);
            NA_CHECK_ERROR(desc_len < 0 || desc_len > NA_BMI_ADDR_NAME_MAX,
                done, ret, NA_OVERFLOW, "Exceeding max addr name");

            bmi_rev_addr = full_rev_addr;
        } else
            bmi_rev_addr = BMI_addr_rev_lookup(na_bmi_addr->bmi_addr);
    }

    string_len = strlen(bmi_rev_addr);
    if (buf) {
        NA_CHECK_ERROR(string_len >= *buf_size, done, ret, NA_OVERFLOW,
            "Buffer size too small to copy addr");
        strcpy(buf, bmi_rev_addr);
    }
    *buf_size = string_len + 1;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static size_t
na_bmi_msg_get_max_unexpected_size(const na_class_t *na_class)
{
    return NA_BMI_CLASS(na_class)->unexpected_size_max;
}

/*---------------------------------------------------------------------------*/
static size_t
na_bmi_msg_get_max_expected_size(const na_class_t *na_class)
{
    return NA_BMI_CLASS(na_class)->expected_size_max;
}

/*---------------------------------------------------------------------------*/
static na_tag_t
na_bmi_msg_get_max_tag(const na_class_t NA_UNUSED *na_class)
{
    return NA_BMI_TAG_MAX;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;
    struct na_bmi_addr *na_bmi_addr = (struct na_bmi_addr *) dest_addr;
    na_return_t ret = NA_SUCCESS;
    int bmi_ret;

    NA_CHECK_ERROR(buf_size > NA_BMI_CLASS(na_class)->unexpected_size_max, done,
        ret, NA_OVERFLOW, "Exceeds unexpected size, %zu", buf_size);

    /* Check op_id */
    NA_CHECK_ERROR(na_bmi_op_id == NULL, done, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_ERROR(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED), done,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed");

    na_bmi_op_id->context = context;
    na_bmi_op_id->completion_data.callback_info.type = NA_CB_SEND_UNEXPECTED;
    na_bmi_op_id->completion_data.callback = callback;
    na_bmi_op_id->completion_data.callback_info.arg = arg;
    hg_atomic_incr32(&na_bmi_addr->ref_count);
    na_bmi_op_id->na_bmi_addr = na_bmi_addr;
    hg_atomic_set32(&na_bmi_op_id->status, 0);

    na_bmi_op_id->info.msg.buf.const_ptr = buf;
    na_bmi_op_id->info.msg.buf_size = (bmi_size_t) buf_size;
    na_bmi_op_id->info.msg.actual_buf_size = (bmi_size_t) buf_size;
    na_bmi_op_id->info.msg.tag = (bmi_msg_tag_t) tag;
    na_bmi_op_id->info.msg.op_id = 0;

    /* Post the BMI unexpected send request */
    bmi_ret = BMI_post_sendunexpected(&na_bmi_op_id->info.msg.op_id,
        na_bmi_addr->bmi_addr, buf, (bmi_size_t) buf_size, BMI_EXT_ALLOC,
        (bmi_msg_tag_t) tag, na_bmi_op_id, NA_BMI_CONTEXT(context)->context_id,
        NULL);
    NA_CHECK_ERROR(bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR,
        "BMI_post_sendunexpected() failed");

    /* If immediate completion, directly add to completion queue */
    if (bmi_ret > 0)
        na_bmi_complete(na_bmi_op_id);

done:
    return ret;

error:
    hg_atomic_decr32(&na_bmi_addr->ref_count);
    hg_atomic_set32(&na_bmi_op_id->status, NA_BMI_OP_COMPLETED);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_op_id_t *op_id)
{
    struct na_bmi_unexpected_msg_queue *unexpected_msg_queue =
        &NA_BMI_CLASS(na_class)->unexpected_msg_queue;
    struct na_bmi_unexpected_info *na_bmi_unexpected_info;
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;
    na_return_t ret = NA_SUCCESS;

    /* Check op_id */
    NA_CHECK_ERROR(na_bmi_op_id == NULL, done, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_ERROR(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED), done,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed");

    na_bmi_op_id->context = context;
    na_bmi_op_id->completion_data.callback_info.type = NA_CB_RECV_UNEXPECTED;
    na_bmi_op_id->completion_data.callback = callback;
    na_bmi_op_id->completion_data.callback_info.arg = arg;
    na_bmi_op_id->na_bmi_addr = NULL;
    hg_atomic_set32(&na_bmi_op_id->status, 0);

    na_bmi_op_id->info.msg.buf.ptr = buf;
    na_bmi_op_id->info.msg.buf_size = (bmi_size_t) buf_size;
    na_bmi_op_id->info.msg.op_id = 0;

    /* Look for an unexpected message already received */
    hg_thread_spin_lock(&unexpected_msg_queue->lock);
    na_bmi_unexpected_info = STAILQ_FIRST(&unexpected_msg_queue->queue);
    if (na_bmi_unexpected_info != NULL)
        STAILQ_REMOVE_HEAD(&unexpected_msg_queue->queue, entry);
    hg_thread_spin_unlock(&unexpected_msg_queue->lock);

    if (unlikely(na_bmi_unexpected_info)) {
        na_bmi_op_id->na_bmi_addr = na_bmi_unexpected_info->na_bmi_addr;
        na_bmi_op_id->info.msg.actual_buf_size =
            na_bmi_unexpected_info->info.size;
        na_bmi_op_id->info.msg.tag = na_bmi_unexpected_info->info.tag;

        /* Copy buffers */
        memcpy(na_bmi_op_id->info.msg.buf.ptr,
            na_bmi_unexpected_info->info.buffer,
            (size_t) na_bmi_unexpected_info->info.size);

        BMI_unexpected_free(na_bmi_unexpected_info->info.addr,
            na_bmi_unexpected_info->info.buffer);
        free(na_bmi_unexpected_info);

        na_bmi_complete(na_bmi_op_id);
    } else {
        struct na_bmi_op_queue *unexpected_op_queue =
            &NA_BMI_CLASS(na_class)->unexpected_op_queue;

        na_bmi_op_id->info.msg.actual_buf_size = 0;
        na_bmi_op_id->info.msg.tag = 0;

        /* Nothing has been received yet so add op_id to progress queue */
        hg_thread_spin_lock(&unexpected_op_queue->lock);
        TAILQ_INSERT_TAIL(&unexpected_op_queue->queue, na_bmi_op_id, entry);
        hg_atomic_or32(&na_bmi_op_id->status, NA_BMI_OP_QUEUED);
        hg_thread_spin_unlock(&unexpected_op_queue->lock);
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_msg_send_expected(na_class_t NA_UNUSED *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;
    struct na_bmi_addr *na_bmi_addr = (struct na_bmi_addr *) dest_addr;
    na_return_t ret = NA_SUCCESS;
    int bmi_ret;

    NA_CHECK_ERROR(buf_size > NA_BMI_CLASS(na_class)->expected_size_max, done,
        ret, NA_OVERFLOW, "Exceeds expected size, %zu", buf_size);

    /* Check op_id */
    NA_CHECK_ERROR(na_bmi_op_id == NULL, done, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_ERROR(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED), done,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed");

    na_bmi_op_id->context = context;
    na_bmi_op_id->completion_data.callback_info.type = NA_CB_SEND_EXPECTED;
    na_bmi_op_id->completion_data.callback = callback;
    na_bmi_op_id->completion_data.callback_info.arg = arg;
    hg_atomic_incr32(&na_bmi_addr->ref_count);
    na_bmi_op_id->na_bmi_addr = na_bmi_addr;
    hg_atomic_set32(&na_bmi_op_id->status, 0);

    na_bmi_op_id->info.msg.buf.const_ptr = buf;
    na_bmi_op_id->info.msg.buf_size = (bmi_size_t) buf_size;
    na_bmi_op_id->info.msg.actual_buf_size = (bmi_size_t) buf_size;
    na_bmi_op_id->info.msg.tag = (bmi_msg_tag_t) tag;
    na_bmi_op_id->info.msg.op_id = 0;

    /* Post the BMI send request */
    bmi_ret =
        BMI_post_send(&na_bmi_op_id->info.msg.op_id, na_bmi_addr->bmi_addr, buf,
            (bmi_size_t) buf_size, BMI_EXT_ALLOC, (bmi_msg_tag_t) tag,
            na_bmi_op_id, NA_BMI_CONTEXT(context)->context_id, NULL);
    NA_CHECK_ERROR(
        bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR, "BMI_post_send() failed");

    /* If immediate completion, directly add to completion queue */
    if (bmi_ret > 0)
        na_bmi_complete(na_bmi_op_id);

done:
    return ret;

error:
    hg_atomic_decr32(&na_bmi_addr->ref_count);
    hg_atomic_set32(&na_bmi_op_id->status, NA_BMI_OP_COMPLETED);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_msg_recv_expected(na_class_t NA_UNUSED *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *source_addr,
    uint8_t NA_UNUSED source_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;
    struct na_bmi_addr *na_bmi_addr = (struct na_bmi_addr *) source_addr;
    na_return_t ret = NA_SUCCESS;
    int bmi_ret;

    /* Check op_id */
    NA_CHECK_ERROR(na_bmi_op_id == NULL, done, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_ERROR(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED), done,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed");

    na_bmi_op_id->context = context;
    na_bmi_op_id->completion_data.callback_info.type = NA_CB_RECV_EXPECTED;
    na_bmi_op_id->completion_data.callback = callback;
    na_bmi_op_id->completion_data.callback_info.arg = arg;
    hg_atomic_incr32(&na_bmi_addr->ref_count);
    na_bmi_op_id->na_bmi_addr = na_bmi_addr;
    hg_atomic_set32(&na_bmi_op_id->status, 0);

    na_bmi_op_id->info.msg.buf.ptr = buf;
    na_bmi_op_id->info.msg.buf_size = (bmi_size_t) buf_size;
    na_bmi_op_id->info.msg.actual_buf_size = 0;
    na_bmi_op_id->info.msg.tag = (bmi_msg_tag_t) tag;
    na_bmi_op_id->info.msg.op_id = 0;

    /* Post the BMI recv request */
    bmi_ret =
        BMI_post_recv(&na_bmi_op_id->info.msg.op_id, na_bmi_addr->bmi_addr, buf,
            (bmi_size_t) buf_size, &na_bmi_op_id->info.msg.actual_buf_size,
            BMI_EXT_ALLOC, (bmi_msg_tag_t) tag, na_bmi_op_id,
            NA_BMI_CONTEXT(context)->context_id, NULL);
    NA_CHECK_ERROR(
        bmi_ret < 0, error, ret, NA_PROTOCOL_ERROR, "BMI_post_recv() failed");

    /* If immediate completion, directly add to completion queue */
    if (bmi_ret > 0)
        na_bmi_complete(na_bmi_op_id);

done:
    return ret;

error:
    hg_atomic_decr32(&na_bmi_addr->ref_count);
    hg_atomic_set32(&na_bmi_op_id->status, NA_BMI_OP_COMPLETED);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_mem_handle_create(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, unsigned long flags, na_mem_handle_t **mem_handle)
{
    struct na_bmi_mem_handle *na_bmi_mem_handle = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Allocate memory handle (use calloc to avoid uninitialized transfer) */
    na_bmi_mem_handle = (struct na_bmi_mem_handle *) calloc(
        1, sizeof(struct na_bmi_mem_handle));
    NA_CHECK_ERROR(na_bmi_mem_handle == NULL, done, ret, NA_NOMEM,
        "Could not allocate NA BMI memory handle");

    na_bmi_mem_handle->base = buf;
    na_bmi_mem_handle->len = (bmi_size_t) buf_size;
    na_bmi_mem_handle->flags = flags & 0xff;

    *mem_handle = (na_mem_handle_t *) na_bmi_mem_handle;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_bmi_mem_handle_free(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    free((struct na_bmi_mem_handle *) mem_handle);
}

/*---------------------------------------------------------------------------*/
static size_t
na_bmi_mem_handle_get_serialize_size(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t NA_UNUSED *mem_handle)
{
    return sizeof(struct na_bmi_mem_handle);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_mem_handle_serialize(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, na_mem_handle_t *mem_handle)
{
    na_return_t ret = NA_SUCCESS;

    NA_CHECK_ERROR(buf_size < sizeof(struct na_bmi_mem_handle), done, ret,
        NA_OVERFLOW, "Buffer size too small for serializing parameter");

    /* Copy struct */
    memcpy(buf, (struct na_bmi_mem_handle *) mem_handle,
        sizeof(struct na_bmi_mem_handle));

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_mem_handle_deserialize(na_class_t NA_UNUSED *na_class,
    na_mem_handle_t **mem_handle, const void *buf, size_t buf_size)
{
    struct na_bmi_mem_handle *na_bmi_mem_handle = NULL;
    na_return_t ret = NA_SUCCESS;

    NA_CHECK_ERROR(buf_size < sizeof(struct na_bmi_mem_handle), done, ret,
        NA_OVERFLOW, "Buffer size too small for deserializing parameter");

    na_bmi_mem_handle =
        (struct na_bmi_mem_handle *) malloc(sizeof(struct na_bmi_mem_handle));
    NA_CHECK_ERROR(na_bmi_mem_handle == NULL, done, ret, NA_NOMEM,
        "Could not allocate NA BMI memory handle");

    /* Copy struct */
    memcpy(na_bmi_mem_handle, buf, sizeof(struct na_bmi_mem_handle));

    *mem_handle = (na_mem_handle_t *) na_bmi_mem_handle;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;
    struct na_bmi_mem_handle *na_bmi_mem_handle_local =
        (struct na_bmi_mem_handle *) local_mem_handle;
    struct na_bmi_mem_handle *na_bmi_mem_handle_remote =
        (struct na_bmi_mem_handle *) remote_mem_handle;
    struct na_bmi_addr *na_bmi_addr = (struct na_bmi_addr *) remote_addr;
    bmi_msg_tag_t rma_tag;
    na_return_t ret = NA_SUCCESS;
    int send_ret, recv_ret;

    switch (na_bmi_mem_handle_remote->flags) {
        case NA_MEM_READ_ONLY:
            NA_GOTO_ERROR(done, ret, NA_PERMISSION,
                "Registered memory requires write permission");
            break;
        case NA_MEM_WRITE_ONLY:
        case NA_MEM_READWRITE:
            break;
        default:
            NA_GOTO_ERROR(
                done, ret, NA_INVALID_ARG, "Invalid memory access flag");
    }

    /* Check overflows */
    NA_CHECK_ERROR(
        (bmi_size_t) (local_offset + length) > na_bmi_mem_handle_local->len,
        done, ret, NA_OVERFLOW,
        "Exceeding length of region exposed (%zu + %zu > %zu)", local_offset,
        length, na_bmi_mem_handle_local->len);
    NA_CHECK_ERROR(
        (bmi_size_t) (remote_offset + length) > na_bmi_mem_handle_remote->len,
        done, ret, NA_OVERFLOW,
        "Exceeding length of region exposed (%zu + %zu > %zu)", remote_offset,
        length, na_bmi_mem_handle_remote->len);

    /* Check op_id */
    NA_CHECK_ERROR(na_bmi_op_id == NULL, done, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_ERROR(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED), done,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed");

    na_bmi_op_id->context = context;
    na_bmi_op_id->completion_data.callback_info.type = NA_CB_PUT;
    na_bmi_op_id->completion_data.callback = callback;
    na_bmi_op_id->completion_data.callback_info.arg = arg;
    hg_atomic_incr32(&na_bmi_addr->ref_count);
    na_bmi_op_id->na_bmi_addr = na_bmi_addr;
    hg_atomic_set32(&na_bmi_op_id->status, 0);

    /* Generate a new base tag */
    rma_tag = na_bmi_gen_rma_tag(na_class);

    /* Fill RMA msg info */
    na_bmi_op_id->info.rma.msg_info.op = NA_BMI_RMA_PUT;
    na_bmi_op_id->info.rma.msg_info.base =
        (char *) na_bmi_mem_handle_remote->base + remote_offset;
    na_bmi_op_id->info.rma.msg_info.len = (bmi_size_t) length;
    na_bmi_op_id->info.rma.msg_info.rma_tag = rma_tag | NA_BMI_RMA_TAG;
    na_bmi_op_id->info.rma.msg_info.ack_tag = rma_tag | NA_BMI_RMA_ACK_TAG;

    na_bmi_op_id->info.rma.base =
        (char *) na_bmi_mem_handle_local->base + local_offset;
    na_bmi_op_id->info.rma.actual_len = 0;
    na_bmi_op_id->info.rma.ack_size = 0;
    na_bmi_op_id->info.rma.msg_op_id = 0;
    na_bmi_op_id->info.rma.rma_op_id = 0;
    na_bmi_op_id->info.rma.ack_op_id = 0;
    hg_atomic_init32(&na_bmi_op_id->info.rma.op_completed_count, 0);
    na_bmi_op_id->info.rma.op_count = 3;
    na_bmi_op_id->info.rma.ack = false;
    na_bmi_op_id->info.rma.flags = 0;

    /* Post the RMA msg request */
    send_ret = BMI_post_sendunexpected(&na_bmi_op_id->info.rma.msg_op_id,
        na_bmi_addr->bmi_addr, &na_bmi_op_id->info.rma.msg_info,
        sizeof(struct na_bmi_rma_msg_info), BMI_EXT_ALLOC,
        rma_tag | NA_BMI_RMA_MSG_TAG, na_bmi_op_id,
        NA_BMI_CONTEXT(context)->context_id, NULL);
    NA_CHECK_ERROR(send_ret < 0, error, ret, NA_PROTOCOL_ERROR,
        "BMI_post_sendunexpected() failed");
    if (send_ret > 0)
        hg_atomic_incr32(&na_bmi_op_id->info.rma.op_completed_count);

    /* Post the RMA ack recv */
    recv_ret = BMI_post_recv(&na_bmi_op_id->info.rma.ack_op_id,
        na_bmi_addr->bmi_addr, &na_bmi_op_id->info.rma.ack, sizeof(bool),
        &na_bmi_op_id->info.rma.ack_size, BMI_EXT_ALLOC,
        na_bmi_op_id->info.rma.msg_info.ack_tag, na_bmi_op_id,
        NA_BMI_CONTEXT(context)->context_id, NULL);
    NA_CHECK_ERROR(
        recv_ret < 0, error, ret, NA_PROTOCOL_ERROR, "BMI_post_recv() failed");

    /* Post the RMA send for the payload */
    send_ret = BMI_post_send(&na_bmi_op_id->info.rma.rma_op_id,
        na_bmi_addr->bmi_addr, na_bmi_op_id->info.rma.base, (bmi_size_t) length,
        BMI_EXT_ALLOC, na_bmi_op_id->info.rma.msg_info.rma_tag, na_bmi_op_id,
        NA_BMI_CONTEXT(context)->context_id, NULL);
    NA_CHECK_ERROR(
        send_ret < 0, error, ret, NA_PROTOCOL_ERROR, "BMI_post_send() failed");
    if (send_ret > 0)
        hg_atomic_incr32(&na_bmi_op_id->info.rma.op_completed_count);

done:
    return ret;

error:
    if (na_bmi_op_id->info.rma.ack_op_id > 0) {
        int bmi_ret;

        na_bmi_op_id->info.rma.op_count--;
        hg_atomic_or32(&na_bmi_op_id->status, NA_BMI_OP_CANCELED);

        bmi_ret = BMI_cancel(na_bmi_op_id->info.rma.ack_op_id,
            NA_BMI_CONTEXT(context)->context_id);
        NA_CHECK_ERROR_DONE(bmi_ret < 0, "BMI_cancel() failed");
    } else {
        hg_atomic_decr32(&na_bmi_addr->ref_count);
        hg_atomic_set32(&na_bmi_op_id->status, NA_BMI_OP_COMPLETED);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;
    struct na_bmi_mem_handle *na_bmi_mem_handle_local =
        (struct na_bmi_mem_handle *) local_mem_handle;
    struct na_bmi_mem_handle *na_bmi_mem_handle_remote =
        (struct na_bmi_mem_handle *) remote_mem_handle;
    struct na_bmi_addr *na_bmi_addr = (struct na_bmi_addr *) remote_addr;
    bmi_msg_tag_t rma_tag;
    na_return_t ret = NA_SUCCESS;
    int send_ret, recv_ret;

    switch (na_bmi_mem_handle_remote->flags) {
        case NA_MEM_WRITE_ONLY:
            NA_GOTO_ERROR(done, ret, NA_PERMISSION,
                "Registered memory requires read permission");
            break;
        case NA_MEM_READ_ONLY:
        case NA_MEM_READWRITE:
            break;
        default:
            NA_GOTO_ERROR(
                done, ret, NA_INVALID_ARG, "Invalid memory access flag");
    }

    /* Check overflows */
    NA_CHECK_ERROR(
        (bmi_size_t) (local_offset + length) > na_bmi_mem_handle_local->len,
        done, ret, NA_OVERFLOW,
        "Exceeding length of region exposed (%zu + %zu > %zu)", local_offset,
        length, na_bmi_mem_handle_local->len);
    NA_CHECK_ERROR(
        (bmi_size_t) (remote_offset + length) > na_bmi_mem_handle_remote->len,
        done, ret, NA_OVERFLOW,
        "Exceeding length of region exposed (%zu + %zu > %zu)", remote_offset,
        length, na_bmi_mem_handle_remote->len);

    /* Check op_id */
    NA_CHECK_ERROR(na_bmi_op_id == NULL, done, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_ERROR(
        !(hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_COMPLETED), done,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed");

    na_bmi_op_id->context = context;
    na_bmi_op_id->completion_data.callback_info.type = NA_CB_GET;
    na_bmi_op_id->completion_data.callback = callback;
    na_bmi_op_id->completion_data.callback_info.arg = arg;
    hg_atomic_incr32(&na_bmi_addr->ref_count);
    na_bmi_op_id->na_bmi_addr = na_bmi_addr;
    hg_atomic_set32(&na_bmi_op_id->status, 0);

    /* Generate a new base tag */
    rma_tag = na_bmi_gen_rma_tag(na_class);

    /* Fill RMA msg info */
    na_bmi_op_id->info.rma.msg_info.op = NA_BMI_RMA_GET;
    na_bmi_op_id->info.rma.msg_info.base =
        (char *) na_bmi_mem_handle_remote->base + remote_offset;
    na_bmi_op_id->info.rma.msg_info.len = (bmi_size_t) length;
    na_bmi_op_id->info.rma.msg_info.rma_tag = rma_tag | NA_BMI_RMA_TAG;
    na_bmi_op_id->info.rma.msg_info.ack_tag = 0;

    na_bmi_op_id->info.rma.base =
        (char *) na_bmi_mem_handle_local->base + local_offset;
    na_bmi_op_id->info.rma.actual_len = 0;
    na_bmi_op_id->info.rma.ack_size = 0;
    na_bmi_op_id->info.rma.msg_op_id = 0;
    na_bmi_op_id->info.rma.rma_op_id = 0;
    na_bmi_op_id->info.rma.ack_op_id = 0;
    hg_atomic_init32(&na_bmi_op_id->info.rma.op_completed_count, 0);
    na_bmi_op_id->info.rma.op_count = 2;
    na_bmi_op_id->info.rma.ack = false;
    na_bmi_op_id->info.rma.flags = 0;

    /* Post the RMA recv for the payload */
    recv_ret = BMI_post_recv(&na_bmi_op_id->info.rma.rma_op_id,
        na_bmi_addr->bmi_addr, na_bmi_op_id->info.rma.base, (bmi_size_t) length,
        &na_bmi_op_id->info.rma.actual_len, BMI_EXT_ALLOC,
        na_bmi_op_id->info.rma.msg_info.rma_tag, na_bmi_op_id,
        NA_BMI_CONTEXT(context)->context_id, NULL);
    NA_CHECK_ERROR(
        recv_ret < 0, error, ret, NA_PROTOCOL_ERROR, "BMI_post_recv() failed");

    /* Post the RMA msg request */
    send_ret = BMI_post_sendunexpected(&na_bmi_op_id->info.rma.msg_op_id,
        na_bmi_addr->bmi_addr, &na_bmi_op_id->info.rma.msg_info,
        sizeof(struct na_bmi_rma_msg_info), BMI_EXT_ALLOC,
        rma_tag | NA_BMI_RMA_MSG_TAG, na_bmi_op_id,
        NA_BMI_CONTEXT(context)->context_id, NULL);
    NA_CHECK_ERROR(send_ret < 0, error, ret, NA_PROTOCOL_ERROR,
        "BMI_post_sendunexpected() failed");
    if (send_ret > 0)
        hg_atomic_incr32(&na_bmi_op_id->info.rma.op_completed_count);

done:
    return ret;

error:
    if (na_bmi_op_id->info.rma.rma_op_id > 0) {
        int bmi_ret;

        na_bmi_op_id->info.rma.op_count--;
        hg_atomic_or32(&na_bmi_op_id->status, NA_BMI_OP_CANCELED);

        bmi_ret = BMI_cancel(na_bmi_op_id->info.rma.rma_op_id,
            NA_BMI_CONTEXT(context)->context_id);
        NA_CHECK_ERROR_DONE(bmi_ret < 0, "BMI_cancel() failed");
    } else {
        hg_atomic_decr32(&na_bmi_addr->ref_count);
        hg_atomic_set32(&na_bmi_op_id->status, NA_BMI_OP_COMPLETED);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p)
{
    return na_bmi_progress(na_class, context, 0, count_p);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_bmi_poll_wait(na_class_t *na_class, na_context_t *context,
    unsigned int timeout_ms, unsigned int *count_p)
{
    hg_time_t deadline, now = hg_time_from_ms(0);
    na_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    do {
        unsigned int count = 0;

        ret = na_bmi_progress(na_class, context,
            hg_time_to_ms(hg_time_subtract(deadline, now)), &count);
        NA_CHECK_NA_ERROR(error, ret, "Could not make expected progress");

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
na_bmi_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t *op_id)
{
    struct na_bmi_op_id *na_bmi_op_id = (struct na_bmi_op_id *) op_id;
    na_return_t ret = NA_SUCCESS;
    int32_t status;

    /* Exit if op has already completed */
    status = hg_atomic_or32(&na_bmi_op_id->status, NA_BMI_OP_CANCELED);
    if ((status & NA_BMI_OP_COMPLETED) || (status & NA_BMI_OP_ERRORED))
        goto done;

    NA_LOG_DEBUG("Canceling operation ID %p", (void *) na_bmi_op_id);

    switch (na_bmi_op_id->completion_data.callback_info.type) {
        case NA_CB_SEND_UNEXPECTED:
        case NA_CB_SEND_EXPECTED:
        case NA_CB_RECV_EXPECTED:
            if (na_bmi_op_id->info.msg.op_id > 0) {
                int bmi_ret = BMI_cancel(na_bmi_op_id->info.msg.op_id,
                    NA_BMI_CONTEXT(context)->context_id);
                NA_CHECK_ERROR(bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR,
                    "BMI_cancel() failed");
            }
            break;

        case NA_CB_RECV_UNEXPECTED: {
            struct na_bmi_op_queue *op_queue =
                &NA_BMI_CLASS(na_class)->unexpected_op_queue;
            bool canceled = false;

            hg_thread_spin_lock(&op_queue->lock);
            if (hg_atomic_get32(&na_bmi_op_id->status) & NA_BMI_OP_QUEUED) {
                TAILQ_REMOVE(&op_queue->queue, na_bmi_op_id, entry);
                hg_atomic_and32(&na_bmi_op_id->status, ~NA_BMI_OP_QUEUED);
                canceled = true;
            }
            hg_thread_spin_unlock(&op_queue->lock);

            if (canceled)
                na_bmi_complete(na_bmi_op_id);
        } break;
        case NA_CB_PUT:
            if (na_bmi_op_id->info.rma.ack_op_id > 0) {
                int bmi_ret = BMI_cancel(na_bmi_op_id->info.rma.ack_op_id,
                    NA_BMI_CONTEXT(context)->context_id);
                NA_CHECK_ERROR(bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR,
                    "BMI_cancel() failed");
            }
            NA_FALLTHROUGH;
        case NA_CB_GET:
            if (na_bmi_op_id->info.rma.msg_op_id > 0) {
                int bmi_ret = BMI_cancel(na_bmi_op_id->info.rma.msg_op_id,
                    NA_BMI_CONTEXT(context)->context_id);
                NA_CHECK_ERROR(bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR,
                    "BMI_cancel() failed");
            }
            if (na_bmi_op_id->info.rma.rma_op_id > 0) {
                int bmi_ret = BMI_cancel(na_bmi_op_id->info.rma.rma_op_id,
                    NA_BMI_CONTEXT(context)->context_id);
                NA_CHECK_ERROR(bmi_ret < 0, done, ret, NA_PROTOCOL_ERROR,
                    "BMI_cancel() failed");
            }
            break;
        default:
            NA_GOTO_ERROR(done, ret, NA_INVALID_ARG,
                "Operation type %d not supported",
                na_bmi_op_id->completion_data.callback_info.type);
    }

done:
    return ret;
}
