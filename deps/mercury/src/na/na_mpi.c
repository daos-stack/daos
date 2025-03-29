/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_mpi.h"
#include "na_plugin.h"

#include "mercury_thread.h"
#include "mercury_thread_condition.h"
#include "mercury_thread_mutex.h"
#include "mercury_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NA_MPI_HAS_GNI_SETUP
#    include <gni_pub.h>
#endif

/****************/
/* Local Macros */
/****************/

/* Error compat */
#define NA_INVALID_PARAM    NA_INVALID_ARG
#define NA_SIZE_ERROR       NA_MSGSIZE
#define NA_NOMEM_ERROR      NA_NOMEM
#define NA_PERMISSION_ERROR NA_PERMISSION

/* MPI initialization flags */
#define MPI_INIT_SERVER 0x01 /* set up to listen for unexpected messages */
#define MPI_INIT_STATIC 0x10 /* set up static inter-communicator */

/* Msg sizes */
#define NA_MPI_UNEXPECTED_SIZE 4096
#define NA_MPI_EXPECTED_SIZE   NA_MPI_UNEXPECTED_SIZE

/* Max tag: computed at init, but standard gives us a reasonable default */
static int MPI_MAX_TAG = 32767;
#define NA_MPI_MAX_TAG (MPI_MAX_TAG >> 2)

/* Default tag used for one-sided over two-sided */
#define NA_MPI_RMA_REQUEST_TAG (NA_MPI_MAX_TAG + 1)
#define NA_MPI_RMA_TAG         (NA_MPI_RMA_REQUEST_TAG + 1)
#define NA_MPI_MAX_RMA_TAG     (MPI_MAX_TAG >> 1)

#define NA_MPI_CLASS(na_class)                                                 \
    ((struct na_mpi_class *) (na_class->plugin_class))

#ifdef _WIN32
#    define strtok_r strtok_s
#    undef strdup
#    define strdup _strdup
#endif

/************************************/
/* Local Type and Struct Definition */
/************************************/

/* na_mpi_addr */
struct na_mpi_addr {
    MPI_Comm comm;     /* Communicator */
    MPI_Comm rma_comm; /* Communicator used for one sided emulation */
    int rank;          /* Rank in this communicator */
    bool unexpected;   /* Address generated from unexpected recv */
    bool self;         /* Boolean for self */
    bool dynamic;      /* Address generated using MPI DPM routines */
    char port_name[MPI_MAX_PORT_NAME]; /* String version of addr */
    LIST_ENTRY(na_mpi_addr) entry;
};

/* na_mpi_mem_handle */
struct na_mpi_mem_handle {
    void *base;    /* Initial address of memory */
    MPI_Aint size; /* Size of memory */
    uint8_t attr;  /* Flag of operation access */
};

/* na_mpi_rma_op */
typedef enum na_mpi_rma_op {
    NA_MPI_RMA_PUT, /* Request a put operation */
    NA_MPI_RMA_GET  /* Request a get operation */
} na_mpi_rma_op_t;

/* na_mpi_rma_info */
struct na_mpi_rma_info {
    na_mpi_rma_op_t op; /* Operation requested */
    void *base;         /* Initial address of memory */
    MPI_Aint disp;      /* Offset from initial address */
    int count;          /* Number of entries */
    na_tag_t tag;       /* Tag used for the data transfer */
};

/* na_mpi_info_send_unexpected */
struct na_mpi_info_send_unexpected {
    MPI_Request data_request;
};

/* na_mpi_info_recv_unexpected */
struct na_mpi_info_recv_unexpected {
    void *buf;
    int buf_size;
    struct na_mpi_addr *remote_addr;
    MPI_Status status;
};

/* na_mpi_info_send_expected */
struct na_mpi_info_send_expected {
    MPI_Request data_request;
};

/* na_mpi_info_recv_expected */
struct na_mpi_info_recv_expected {
    MPI_Request data_request;
    int buf_size;
    int actual_size;
    MPI_Status status;
};

/* na_mpi_info_put */
struct na_mpi_info_put {
    MPI_Request rma_request;
    MPI_Request data_request;
    struct na_mpi_rma_info *rma_info;
    bool internal_progress; /* Used for internal RMA emulation */
};

/* na_mpi_info_get */
struct na_mpi_info_get {
    MPI_Request rma_request;
    MPI_Request data_request;
    struct na_mpi_rma_info *rma_info;
    bool internal_progress; /* Used for internal RMA emulation */
};

struct na_mpi_op_id {
    na_context_t *context;
    na_cb_type_t type;
    na_cb_t callback; /* Callback */
    void *arg;
    hg_atomic_int32_t completed; /* Operation completed */
    bool canceled;               /* Operation canceled */
    union {
        struct na_mpi_info_send_unexpected send_unexpected;
        struct na_mpi_info_recv_unexpected recv_unexpected;
        struct na_mpi_info_send_expected send_expected;
        struct na_mpi_info_recv_expected recv_expected;
        struct na_mpi_info_put put;
        struct na_mpi_info_get get;
    } info;
    TAILQ_ENTRY(na_mpi_op_id) entry;
    struct na_cb_completion_data completion_data;
};

struct na_mpi_class {
    bool listening;                    /* Used in server mode */
    bool mpi_ext_initialized;          /* MPI externally initialized */
    bool use_static_inter_comm;        /* Use static inter-communicator */
    char port_name[MPI_MAX_PORT_NAME]; /* Server local port name used for
                                          dynamic connection */
    MPI_Comm intra_comm;               /* MPI intra-communicator */

    size_t unexpected_size_max; /* Max unexpected size */
    size_t expected_size_max;   /* Max expected size */

    hg_thread_t accept_thread;      /* Thread for accepting new connections */
    hg_thread_mutex_t accept_mutex; /* Mutex */
    hg_thread_cond_t accept_cond;   /* Cond */
    bool accepting;                 /* Is in MPI_Comm_accept */

    LIST_HEAD(, na_mpi_addr) remote_list; /* List of connected remotes */
    hg_thread_mutex_t remote_list_mutex;  /* Mutex */

    TAILQ_HEAD(, na_mpi_op_id) unexpected_op_queue; /* Unexpected op queue */
    hg_thread_mutex_t unexpected_op_queue_mutex;    /* Mutex */

    hg_atomic_int32_t rma_tag; /* Atomic RMA tag value */

    TAILQ_HEAD(, na_mpi_op_id) op_id_list; /* List of na_mpi_op_ids */
    hg_thread_mutex_t op_id_list_mutex;    /* Mutex */
};

/********************/
/* Local Prototypes */
/********************/

/* accept_service */
static HG_THREAD_RETURN_TYPE
na_mpi_accept_service(void *args);

/* open_port */
static na_return_t
na_mpi_open_port(struct na_mpi_class *na_mpi_class);

/* get_port_info */
static na_return_t
na_mpi_get_port_info(const char *name, char *mpi_port_name, int *mpi_rank);

/* accept */
static na_return_t
na_mpi_accept(struct na_mpi_class *na_mpi_class);

/* disconnect */
static na_return_t
na_mpi_disconnect(na_class_t *na_class, struct na_mpi_addr *na_mpi_addr);

/* remote_list_disconnect */
static na_return_t
na_mpi_remote_list_disconnect(na_class_t *na_class);

/* msg_unexpected_op_push */
static void
na_mpi_msg_unexpected_op_push(
    na_class_t *na_class, struct na_mpi_op_id *na_mpi_op_id);

/* msg_unexpected_op_pop */
static struct na_mpi_op_id *
na_mpi_msg_unexpected_op_pop(na_class_t *na_class);

/* gen_rma_tag */
static NA_INLINE na_tag_t
na_mpi_gen_rma_tag(na_class_t *na_class);

/* verify */
static bool
na_mpi_check_protocol(const char *protocol_name);

/* initialize */
static na_return_t
na_mpi_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen);

/* finalize */
static na_return_t
na_mpi_finalize(na_class_t *na_class);

/* op_create */
static na_op_id_t *
na_mpi_op_create(na_class_t *na_class, unsigned long flags);

/* op_destroy */
static void
na_mpi_op_destroy(na_class_t *na_class, na_op_id_t *op_id);

/* addr_lookup */
static na_return_t
na_mpi_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr);

/* addr_self */
static na_return_t
na_mpi_addr_self(na_class_t *na_class, na_addr_t **addr);

/* addr_free */
static void
na_mpi_addr_free(na_class_t *na_class, na_addr_t *addr);

/* addr_cmp */
static bool
na_mpi_addr_cmp(na_class_t *na_class, na_addr_t *addr1, na_addr_t *addr2);

/* addr_is_self */
static bool
na_mpi_addr_is_self(na_class_t *na_class, na_addr_t *addr);

/* addr_to_string */
static na_return_t
na_mpi_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr);

/* msg_get_max */
static size_t
na_mpi_msg_get_max_unexpected_size(const na_class_t *na_class);

static size_t
na_mpi_msg_get_max_expected_size(const na_class_t *na_class);

static na_tag_t
na_mpi_msg_get_max_tag(const na_class_t *na_class);

/* msg_send_unexpected */
static na_return_t
na_mpi_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_unexpected */
static na_return_t
na_mpi_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id);

/* msg_send_expected */
static na_return_t
na_mpi_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_expected */
static na_return_t
na_mpi_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_addr_t *source_addr, uint8_t source_id, na_tag_t tag, na_op_id_t *op_id);

/* mem_handle */
static na_return_t
na_mpi_mem_handle_create(na_class_t *na_class, void *buf, size_t buf_size,
    unsigned long flags, na_mem_handle_t **mem_handle);

static void
na_mpi_mem_handle_free(na_class_t *na_class, na_mem_handle_t *mem_handle);

/* mem_handle serialization */
static size_t
na_mpi_mem_handle_get_serialize_size(
    na_class_t *na_class, na_mem_handle_t *mem_handle);

static na_return_t
na_mpi_mem_handle_serialize(na_class_t *na_class, void *buf, size_t buf_size,
    na_mem_handle_t *mem_handle);

static na_return_t
na_mpi_mem_handle_deserialize(na_class_t *na_class,
    na_mem_handle_t **mem_handle, const void *buf, size_t buf_size);

/* put */
static na_return_t
na_mpi_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* get */
static na_return_t
na_mpi_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* poll */
static na_return_t
na_mpi_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p);

/* na_mpi_progress_unexpected */
static na_return_t
na_mpi_progress_unexpected(
    na_class_t *na_class, na_context_t *context, unsigned int timeout);

/* na_mpi_progress_unexpected_msg */
static na_return_t
na_mpi_progress_unexpected_msg(na_class_t *na_class, na_context_t *context,
    struct na_mpi_addr *na_mpi_addr, const MPI_Status *status);

/* na_mpi_progress_unexpected_rma */
static na_return_t
na_mpi_progress_unexpected_rma(na_class_t *na_class, na_context_t *context,
    struct na_mpi_addr *na_mpi_addr, const MPI_Status *status);

/* na_mpi_progress_expected */
static na_return_t
na_mpi_progress_expected(
    na_class_t *na_class, na_context_t *context, unsigned int timeout);

/* na_mpi_complete */
static na_return_t
na_mpi_complete(struct na_mpi_op_id *na_mpi_op_id);

/* na_mpi_release */
static void
na_mpi_release(void *arg);

/* cancel */
static na_return_t
na_mpi_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t *op_id);

/*******************/
/* Local Variables */
/*******************/

const struct na_class_ops NA_PLUGIN_OPS(mpi) = {
    "mpi",                                /* name */
    NULL,                                 /* get_protocol_info */
    na_mpi_check_protocol,                /* check_protocol */
    na_mpi_initialize,                    /* initialize */
    na_mpi_finalize,                      /* finalize */
    NULL,                                 /* cleanup */
    NULL,                                 /* has_opt_feature */
    NULL,                                 /* context_create */
    NULL,                                 /* context_destroy */
    na_mpi_op_create,                     /* op_create */
    na_mpi_op_destroy,                    /* op_destroy */
    na_mpi_addr_lookup,                   /* addr_lookup */
    na_mpi_addr_free,                     /* addr_free */
    NULL,                                 /* addr_set_remove */
    na_mpi_addr_self,                     /* addr_self */
    NULL,                                 /* addr_dup */
    na_mpi_addr_cmp,                      /* addr_cmp */
    na_mpi_addr_is_self,                  /* addr_is_self */
    na_mpi_addr_to_string,                /* addr_to_string */
    NULL,                                 /* addr_get_serialize_size */
    NULL,                                 /* addr_serialize */
    NULL,                                 /* addr_deserialize */
    na_mpi_msg_get_max_unexpected_size,   /* msg_get_max_unexpected_size */
    na_mpi_msg_get_max_expected_size,     /* msg_get_max_expected_size */
    NULL,                                 /* msg_get_unexpected_header_size */
    NULL,                                 /* msg_get_expected_header_size */
    na_mpi_msg_get_max_tag,               /* msg_get_max_tag */
    NULL,                                 /* msg_buf_alloc */
    NULL,                                 /* msg_buf_free */
    NULL,                                 /* msg_init_unexpected */
    na_mpi_msg_send_unexpected,           /* msg_send_unexpected */
    na_mpi_msg_recv_unexpected,           /* msg_recv_unexpected */
    NULL,                                 /* msg_multi_recv_unexpected */
    NULL,                                 /* msg_init_expected */
    na_mpi_msg_send_expected,             /* msg_send_expected */
    na_mpi_msg_recv_expected,             /* msg_recv_expected */
    na_mpi_mem_handle_create,             /* mem_handle_create */
    NULL,                                 /* mem_handle_create_segment */
    na_mpi_mem_handle_free,               /* mem_handle_free */
    NULL,                                 /* mem_handle_get_max_segments */
    NULL,                                 /* mem_register */
    NULL,                                 /* mem_deregister */
    na_mpi_mem_handle_get_serialize_size, /* mem_handle_get_serialize_size */
    na_mpi_mem_handle_serialize,          /* mem_handle_serialize */
    na_mpi_mem_handle_deserialize,        /* mem_handle_deserialize */
    na_mpi_put,                           /* put */
    na_mpi_get,                           /* get */
    NULL,                                 /* poll_get_fd */
    NULL,                                 /* poll_try_wait */
    na_mpi_poll,                          /* poll */
    NULL,                                 /* poll_wait */
    na_mpi_cancel                         /* cancel */
};

static MPI_Comm na_mpi_init_comm_g = MPI_COMM_NULL; /* MPI comm used at init */

#ifdef NA_MPI_HAS_GNI_SETUP
const uint8_t ptag_value = 20;
const uint32_t key_value = GNI_PKEY_USER_START + 1;
#endif

/********************/
/* Plugin callbacks */
/********************/

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
na_mpi_accept_service(void *args)
{
    hg_thread_ret_t ret = 0;
    na_return_t na_ret;

    na_ret = na_mpi_accept((struct na_mpi_class *) args);
    if (na_ret != NA_SUCCESS) {
        NA_LOG_ERROR("Could not accept connection");
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_open_port(struct na_mpi_class *na_mpi_class)
{
    char mpi_port_name[MPI_MAX_PORT_NAME];
    int my_rank;
    int mpi_ret;
    na_return_t ret = NA_SUCCESS;

    memset(na_mpi_class->port_name, '\0', MPI_MAX_PORT_NAME);
    memset(mpi_port_name, '\0', MPI_MAX_PORT_NAME);

    MPI_Comm_rank(na_mpi_class->intra_comm, &my_rank);
    if (my_rank == 0) {
        mpi_ret = MPI_Open_port(MPI_INFO_NULL, mpi_port_name);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("MPI_Open_port failed");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }
    }
    mpi_ret = MPI_Bcast(mpi_port_name, MPI_MAX_PORT_NAME, MPI_BYTE, 0,
        na_mpi_class->intra_comm);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Bcast() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    strcpy(na_mpi_class->port_name, mpi_port_name);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_get_port_info(const char *name, char *mpi_port_name, int *mpi_rank)
{
    char *port_string = NULL, *rank_string = NULL, *rank_value = NULL;
    char *dup_name;
    na_return_t ret = NA_SUCCESS;

    dup_name = strdup(name);
    if (!dup_name) {
        NA_LOG_ERROR("Cannot dup name");
        ret = NA_NOMEM_ERROR;
        goto done;
    }

    /* Get mpi port name */
    port_string = strtok_r(dup_name, ";", &rank_string);
    strcpy(mpi_port_name, port_string);

    if (!rank_string) {
        NA_LOG_ERROR("Cannot get rank from port name info");
        ret = NA_INVALID_PARAM;
        goto done;
    }

    /* Get rank info */
    if (strlen(rank_string)) {
        rank_string = strtok_r(rank_string, "$", &rank_value);
        rank_string = strtok_r(rank_string, "#", &rank_value);

        if (rank_value && strcmp(rank_string, "rank") == 0) {
            if (mpi_rank)
                *mpi_rank = atoi(rank_value);
        } else {
            if (mpi_rank)
                *mpi_rank = 0;
        }
    }

done:
    free(dup_name);
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_accept(struct na_mpi_class *na_mpi_class)
{
    MPI_Comm new_comm;
    MPI_Comm new_rma_comm;
    struct na_mpi_addr *na_mpi_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    hg_thread_mutex_lock(&na_mpi_class->accept_mutex);

    if (na_mpi_class->use_static_inter_comm) {
        int global_size, intra_size;

        MPI_Comm_size(MPI_COMM_WORLD, &global_size);
        MPI_Comm_size(na_mpi_class->intra_comm, &intra_size);
        mpi_ret =
            MPI_Intercomm_create(na_mpi_class->intra_comm, 0, MPI_COMM_WORLD,
                global_size - (global_size - intra_size), 0, &new_comm);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("MPI_Intercomm_create failed");
            ret = NA_PROTOCOL_ERROR;
            hg_thread_mutex_unlock(&na_mpi_class->accept_mutex);
            goto done;
        }
    } else {
        mpi_ret = MPI_Comm_accept(na_mpi_class->port_name, MPI_INFO_NULL, 0,
            na_mpi_class->intra_comm, &new_comm);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("MPI_Comm_accept failed");
            ret = NA_PROTOCOL_ERROR;
            hg_thread_mutex_unlock(&na_mpi_class->accept_mutex);
            goto done;
        }
    }

    /* To be thread-safe and create a new context, dup the remote comm to a
     * new comm */
    mpi_ret = MPI_Comm_dup(new_comm, &new_rma_comm);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Comm_dup() failed");
        ret = NA_PROTOCOL_ERROR;
        hg_thread_mutex_unlock(&na_mpi_class->accept_mutex);
        goto done;
    }

    na_mpi_class->accepting = false;
    hg_thread_cond_signal(&na_mpi_class->accept_cond);

    hg_thread_mutex_unlock(&na_mpi_class->accept_mutex);

    na_mpi_addr = (struct na_mpi_addr *) malloc(sizeof(struct na_mpi_addr));
    if (!na_mpi_addr) {
        NA_LOG_ERROR("Could not allocate mpi_addr");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    na_mpi_addr->comm = new_comm;
    na_mpi_addr->rma_comm = new_rma_comm;
    na_mpi_addr->rank = MPI_ANY_SOURCE;
    na_mpi_addr->unexpected = false;
    na_mpi_addr->dynamic = (bool) (!na_mpi_class->use_static_inter_comm);
    memset(na_mpi_addr->port_name, '\0', MPI_MAX_PORT_NAME);

    /* Add comms to list of connected remotes */
    hg_thread_mutex_lock(&na_mpi_class->remote_list_mutex);
    LIST_INSERT_HEAD(&na_mpi_class->remote_list, na_mpi_addr, entry);
    hg_thread_mutex_unlock(&na_mpi_class->remote_list_mutex);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_disconnect(
    na_class_t NA_UNUSED *na_class, struct na_mpi_addr *na_mpi_addr)
{
    na_return_t ret = NA_SUCCESS;

    if (na_mpi_addr && !na_mpi_addr->unexpected) {
        MPI_Comm_free(&na_mpi_addr->rma_comm);

        if (na_mpi_addr->dynamic) {
            int mpi_ret;

            mpi_ret = MPI_Comm_disconnect(&na_mpi_addr->comm);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Comm_disconnect() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
        } else {
            MPI_Comm_free(&na_mpi_addr->comm);
        }
    }
    free(na_mpi_addr);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_remote_list_disconnect(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;

    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->remote_list_mutex);

    /* Process list of communicators */
    while (!LIST_EMPTY(&NA_MPI_CLASS(na_class)->remote_list)) {
        struct na_mpi_addr *na_mpi_addr =
            LIST_FIRST(&NA_MPI_CLASS(na_class)->remote_list);
        LIST_REMOVE(na_mpi_addr, entry);

        ret = na_mpi_disconnect(na_class, na_mpi_addr);
        if (ret != NA_SUCCESS) {
            goto done;
        }
    }

done:
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->remote_list_mutex);
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_mpi_msg_unexpected_op_push(
    na_class_t *na_class, struct na_mpi_op_id *na_mpi_op_id)
{
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->unexpected_op_queue_mutex);

    TAILQ_INSERT_TAIL(
        &NA_MPI_CLASS(na_class)->unexpected_op_queue, na_mpi_op_id, entry);

    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->unexpected_op_queue_mutex);
}

/*---------------------------------------------------------------------------*/
static struct na_mpi_op_id *
na_mpi_msg_unexpected_op_pop(na_class_t *na_class)
{
    struct na_mpi_op_id *na_mpi_op_id;

    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->unexpected_op_queue_mutex);

    na_mpi_op_id = TAILQ_FIRST(&NA_MPI_CLASS(na_class)->unexpected_op_queue);
    TAILQ_REMOVE(
        &NA_MPI_CLASS(na_class)->unexpected_op_queue, na_mpi_op_id, entry);

    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->unexpected_op_queue_mutex);

    return na_mpi_op_id;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_tag_t
na_mpi_gen_rma_tag(na_class_t *na_class)
{
    na_tag_t tag;

    /* Compare and swap tag if reached max tag */
    if (hg_atomic_cas32(&NA_MPI_CLASS(na_class)->rma_tag, NA_MPI_MAX_RMA_TAG,
            NA_MPI_RMA_TAG)) {
        tag = (na_tag_t) NA_MPI_RMA_TAG;
    } else {
        /* Increment tag */
        tag = (na_tag_t) hg_atomic_incr32(&NA_MPI_CLASS(na_class)->rma_tag);
    }

    return tag;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_MPI_Set_init_intra_comm(MPI_Comm intra_comm)
{
    na_mpi_init_comm_g = intra_comm;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
const char *
NA_MPI_Get_port_name(na_class_t *na_class)
{
    int my_rank;
    static char port_name[MPI_MAX_PORT_NAME + 16];

    MPI_Comm_rank(NA_MPI_CLASS(na_class)->intra_comm, &my_rank);

    /* Append rank info to port name */
    if (NA_MPI_CLASS(na_class)->use_static_inter_comm)
        sprintf(port_name, "rank#%d$", my_rank);
    else
        sprintf(port_name, "%s;rank#%d$", NA_MPI_CLASS(na_class)->port_name,
            my_rank);

    return port_name;
}

/*---------------------------------------------------------------------------*/
#ifdef NA_MPI_HAS_GNI_SETUP
static na_return_t
gni_job_setup(uint8_t ptag, uint32_t cookie)
{
    gni_return_t grc;
    gni_job_limits_t limits;
    na_return_t ret = NA_SUCCESS;

    /* Do not apply any resource limits */
    limits.a.mrt_limit = GNI_JOB_INVALID_LIMIT;
    limits.b.gart_limit = GNI_JOB_INVALID_LIMIT;
    limits.mdd_limit = GNI_JOB_INVALID_LIMIT;
    limits.fma_limit = GNI_JOB_INVALID_LIMIT;
    limits.bte_limit = GNI_JOB_INVALID_LIMIT;
    limits.cq_limit = GNI_JOB_INVALID_LIMIT;

    /* Do not use NTT */
    limits.ntt_size = 0;

    /* GNI_ConfigureJob():
     * -device_id should be 0 for XC since we only have 1 NIC/node
     * -job_id should always be 0 (meaning "no job container created")
     */
    grc = GNI_ConfigureJob(0, 0, ptag, cookie, &limits);
    if (grc == GNI_RC_PERMISSION_ERROR) {
        NA_LOG_ERROR("GNI_ConfigureJob(...) requires root privileges.");
        ret = NA_PERMISSION_ERROR;
    }
    NA_LOG_DEBUG("GNI_ConfigureJob returned %s", gni_err_str[grc]);

    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_MPI_Gni_job_setup(void)
{
    char ptag_string[128];
    char cookie_string[128];
    uint32_t cookie_value = GNI_JOB_CREATE_COOKIE(key_value, 0);
    na_return_t ret;

    if ((key_value < GNI_PKEY_USER_START) && (key_value >= GNI_PKEY_USER_END)) {
        NA_LOG_ERROR("Invalid key value");
        ret = NA_INVALID_PARAM;
        goto done;
    }
    if ((ptag_value < GNI_PTAG_USER_START) &&
        (ptag_value >= GNI_PTAG_USER_END)) {
        NA_LOG_ERROR("Invalid ptag value");
        ret = NA_INVALID_PARAM;
        goto done;
    }

    /*
     * setup ptag/pcookie  env variables for MPI
     */
    sprintf(ptag_string, "PMI_GNI_PTAG=%d", ptag_value);
    putenv(ptag_string);
    sprintf(cookie_string, "PMI_GNI_COOKIE=%d", cookie_value);
    putenv(cookie_string);

    NA_LOG_DEBUG(
        "Setting ptag to %d and cookie to 0x%x", ptag_value, cookie_value);
    NA_LOG_DEBUG("sanity check PMI_GNI_PTAG = %s", getenv("PMI_GNI_PTAG"));
    NA_LOG_DEBUG("sanity check PMI_GNI_COOKIE = %s", getenv("PMI_GNI_COOKIE"));

    /*
     * setup the Aries NIC resources for the job (this can be done multiple
     * times for the same ptag/cookie combination on the same node), so it
     * doesn't matter if there are multiple MPI ranks per node.
     */
    ret = gni_job_setup(ptag_value, cookie_value);

done:
    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
static bool
na_mpi_check_protocol(const char NA_UNUSED *protocol_name)
{
    if (protocol_name == NULL || (strcmp(protocol_name, "dynamic") != 0 &&
                                     strcmp(protocol_name, "static") != 0))
        return false;
    else
        return true;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen)
{
    const struct na_init_info *na_init_info = &na_info->na_init_info;
    struct na_mpi_class *na_mpi_class = NULL;
    int mpi_ext_initialized = 0;
    bool listening, use_static_inter_comm;
    int flags = (listen) ? MPI_INIT_SERVER : 0;
    int mpi_ret;
    int *attr_val, attr_flag;
    na_return_t ret = NA_SUCCESS;

    na_mpi_class = (struct na_mpi_class *) calloc(1, sizeof(*na_mpi_class));
    na_class->plugin_class = (void *) na_mpi_class;
    if (!na_mpi_class) {
        NA_LOG_ERROR("Could not allocate NA private data class");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    na_mpi_class->accept_thread = 0;
    LIST_INIT(&na_mpi_class->remote_list);
    TAILQ_INIT(&na_mpi_class->op_id_list);
    TAILQ_INIT(&na_mpi_class->unexpected_op_queue);

    /* Initialize mutex/cond */
    hg_thread_mutex_init(&na_mpi_class->accept_mutex);
    hg_thread_cond_init(&na_mpi_class->accept_cond);
    hg_thread_mutex_init(&na_mpi_class->remote_list_mutex);
    hg_thread_mutex_init(&na_mpi_class->op_id_list_mutex);
    hg_thread_mutex_init(&na_mpi_class->unexpected_op_queue_mutex);

    /* Check flags */
    if (strcmp(na_info->protocol_name, "static") == 0)
        flags |= MPI_INIT_STATIC;
    else if (strcmp(na_info->protocol_name, "dynamic") != 0) {
        NA_LOG_ERROR(
            "Unknown protocol name for MPI, "
            "expected \"dynamic\" or \"static\". Falling back to dynamic");
        goto done;
    }

    /* ensure user didn't pass in a host string (it's ignored) */
    if (na_info->host_name != NULL) {
        NA_LOG_ERROR("Host name is unused when initializing MPI");
        goto done;
    }

    listening = (bool) (flags & MPI_INIT_SERVER);
    na_mpi_class->listening = listening;

    use_static_inter_comm = (bool) (flags & MPI_INIT_STATIC);
    na_mpi_class->use_static_inter_comm = use_static_inter_comm;

    /* Set msg size limits */
    na_mpi_class->unexpected_size_max = na_init_info->max_unexpected_size
                                            ? na_init_info->max_unexpected_size
                                            : NA_MPI_UNEXPECTED_SIZE;
    na_mpi_class->expected_size_max = na_init_info->max_expected_size
                                          ? na_init_info->max_expected_size
                                          : NA_MPI_EXPECTED_SIZE;

    /* Initialize MPI */
    mpi_ret = MPI_Initialized(&mpi_ext_initialized);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Initialized failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }
    na_mpi_class->mpi_ext_initialized = (bool) mpi_ext_initialized;

    if (!mpi_ext_initialized) {
        int provided;
#ifdef NA_MPI_HAS_GNI_SETUP
        /* Setup GNI job before initializing MPI */
        if (NA_MPI_Gni_job_setup() != NA_SUCCESS) {
            NA_LOG_ERROR("Could not setup GNI job");
            error_occurred = true;
            goto done;
        }
#endif
        /* Listening implies creation of listening thread so use that to
         * be safe */
        mpi_ret = MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);
        if (provided != MPI_THREAD_MULTIPLE) {
            NA_LOG_ERROR("MPI_THREAD_MULTIPLE cannot be set");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("Could not initialize MPI");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }
    }

    /* Assign MPI intra comm */
    if ((na_mpi_init_comm_g != MPI_COMM_NULL) || !use_static_inter_comm) {
        MPI_Comm comm = (na_mpi_init_comm_g != MPI_COMM_NULL)
                            ? na_mpi_init_comm_g
                            : MPI_COMM_WORLD;

        mpi_ret = MPI_Comm_dup(comm, &na_mpi_class->intra_comm);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("Could not duplicate communicator");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }
    } else if (use_static_inter_comm) {
        int color;
        int global_rank;

        MPI_Comm_rank(MPI_COMM_WORLD, &global_rank);
        /* Color is 1 for server, 2 for client */
        color = (listening) ? 1 : 2;

        /* Assume that the application did not split MPI_COMM_WORLD already */
        mpi_ret = MPI_Comm_split(
            MPI_COMM_WORLD, color, global_rank, &na_mpi_class->intra_comm);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("Could not split communicator");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }
    }

    /* Initialize atomic op */
    hg_atomic_set32(&na_mpi_class->rma_tag, NA_MPI_RMA_TAG);

    /* If server opens a port */
    if (listening) {
        na_mpi_class->accepting = true;
        if (!use_static_inter_comm &&
            (ret = na_mpi_open_port(na_mpi_class)) != NA_SUCCESS) {
            NA_LOG_ERROR("Cannot open port");
            goto done;
        }

        /* We need to create a thread here if we want to allow
         * connection / disconnection since MPI does not provide any
         * service for that and MPI_Comm_accept is blocking */
        hg_thread_create(&na_mpi_class->accept_thread, &na_mpi_accept_service,
            (void *) na_mpi_class);
    } else {
        na_mpi_class->accepting = false;
    }

    /* MPI implementation typically provides a "max tag" far larger than
     * standard demands */
    MPI_Comm_get_attr(
        na_mpi_class->intra_comm, MPI_TAG_UB, &attr_val, &attr_flag);
    if (attr_flag)
        MPI_MAX_TAG = *attr_val;

done:
    if (ret != NA_SUCCESS) {
        na_mpi_finalize(na_class);
        na_class->plugin_class = NULL;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_finalize(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;
    int mpi_ext_finalized = 0;
    int mpi_ret;

    if (!na_class->plugin_class) {
        goto done;
    }

    if (NA_MPI_CLASS(na_class)->listening) {
        /* No more connection accepted after this point */
        hg_thread_join(NA_MPI_CLASS(na_class)->accept_thread);

        /* If server opened a port */
        if (!NA_MPI_CLASS(na_class)->use_static_inter_comm) {
            mpi_ret = MPI_Close_port(NA_MPI_CLASS(na_class)->port_name);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("Could not close port");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
        }
    }
    /* Process list of communicators */
    na_mpi_remote_list_disconnect(na_class);

    /* Check that unexpected op queue is empty */
    if (!TAILQ_EMPTY(&NA_MPI_CLASS(na_class)->unexpected_op_queue)) {
        NA_LOG_ERROR("Unexpected op queue should be empty");
        ret = NA_PROTOCOL_ERROR;
    }

    /* Free the private dup'ed comm */
    mpi_ret = MPI_Comm_free(&NA_MPI_CLASS(na_class)->intra_comm);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("Could not free intra_comm");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* MPI_Finalize */
    MPI_Finalized(&mpi_ext_finalized);
    if (mpi_ext_finalized) {
        NA_LOG_ERROR("MPI already finalized");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    if (!NA_MPI_CLASS(na_class)->mpi_ext_initialized && !mpi_ext_finalized) {
        mpi_ret = MPI_Finalize();
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("Could not finalize MPI");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }
    }

    /* Destroy mutex/cond */
    hg_thread_mutex_destroy(&NA_MPI_CLASS(na_class)->accept_mutex);
    hg_thread_cond_destroy(&NA_MPI_CLASS(na_class)->accept_cond);
    hg_thread_mutex_destroy(&NA_MPI_CLASS(na_class)->remote_list_mutex);
    hg_thread_mutex_destroy(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    hg_thread_mutex_destroy(&NA_MPI_CLASS(na_class)->unexpected_op_queue_mutex);

    free(na_class->plugin_class);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_op_id_t *
na_mpi_op_create(na_class_t NA_UNUSED *na_class, unsigned long NA_UNUSED flags)
{
    struct na_mpi_op_id *na_mpi_op_id = NULL;

    na_mpi_op_id = (struct na_mpi_op_id *) malloc(sizeof(struct na_mpi_op_id));
    if (!na_mpi_op_id) {
        NA_LOG_ERROR("Could not allocate NA MPI operation ID");
        goto done;
    }
    memset(na_mpi_op_id, 0, sizeof(struct na_mpi_op_id));

    /* Completed by default */
    hg_atomic_init32(&na_mpi_op_id->completed, 1);

done:
    return (na_op_id_t *) na_mpi_op_id;
}

/*---------------------------------------------------------------------------*/
static void
na_mpi_op_destroy(na_class_t NA_UNUSED *na_class, na_op_id_t *op_id)
{
    free((struct na_mpi_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr)
{
    struct na_mpi_addr *na_mpi_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    /* Allocate addr */
    na_mpi_addr = (struct na_mpi_addr *) malloc(sizeof(struct na_mpi_addr));
    if (!na_mpi_addr) {
        NA_LOG_ERROR("Could not allocate addr");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    na_mpi_addr->rank = 0;
    na_mpi_addr->comm = MPI_COMM_NULL;
    na_mpi_addr->rma_comm = MPI_COMM_NULL;
    na_mpi_addr->unexpected = false;
    na_mpi_addr->self = false;
    na_mpi_addr->dynamic = false;

    memset(na_mpi_addr->port_name, '\0', MPI_MAX_PORT_NAME);
    /* get port_name and remote server rank */
    na_mpi_get_port_info(name, na_mpi_addr->port_name, &na_mpi_addr->rank);

    /* Try to connect, must prevent concurrent threads to
     * create new communicators */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->accept_mutex);

    /* TODO A listening process can only "connect" to one of his pairs ? */
    if (NA_MPI_CLASS(na_class)->listening) {
        while (NA_MPI_CLASS(na_class)->accepting) {
            hg_thread_cond_wait(&NA_MPI_CLASS(na_class)->accept_cond,
                &NA_MPI_CLASS(na_class)->accept_mutex);
        }
        mpi_ret = MPI_Comm_dup(
            NA_MPI_CLASS(na_class)->intra_comm, &na_mpi_addr->comm);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("MPI_Comm_dup() failed");
            ret = NA_PROTOCOL_ERROR;
            hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->accept_mutex);
            goto done;
        }
    } else {
        if (NA_MPI_CLASS(na_class)->use_static_inter_comm) {
            mpi_ret = MPI_Intercomm_create(NA_MPI_CLASS(na_class)->intra_comm,
                0, MPI_COMM_WORLD, 0, 0, &na_mpi_addr->comm);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Intercomm_create() failed");
                ret = NA_PROTOCOL_ERROR;
                hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->accept_mutex);
                goto done;
            }
        } else {
            na_mpi_addr->dynamic = true;
            mpi_ret = MPI_Comm_connect(na_mpi_addr->port_name, MPI_INFO_NULL, 0,
                NA_MPI_CLASS(na_class)->intra_comm, &na_mpi_addr->comm);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Comm_connect() failed");
                ret = NA_PROTOCOL_ERROR;
                hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->accept_mutex);
                goto done;
            }
        }
    }

    /* To be thread-safe and create a new context,
     * dup the remote comm to a new comm */
    mpi_ret = MPI_Comm_dup(na_mpi_addr->comm, &na_mpi_addr->rma_comm);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Comm_dup() failed");
        ret = NA_PROTOCOL_ERROR;
        hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->accept_mutex);
        goto done;
    }

    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->accept_mutex);

    /* Add addr to list of addresses */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->remote_list_mutex);
    LIST_INSERT_HEAD(&NA_MPI_CLASS(na_class)->remote_list, na_mpi_addr, entry);
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->remote_list_mutex);

    *addr = (na_addr_t *) na_mpi_addr;

done:
    if (ret != NA_SUCCESS) {
        free(na_mpi_addr);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_addr_self(na_class_t *na_class, na_addr_t **addr)
{
    struct na_mpi_addr *na_mpi_addr = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Allocate addr */
    na_mpi_addr = (struct na_mpi_addr *) malloc(sizeof(struct na_mpi_addr));
    if (!na_mpi_addr) {
        NA_LOG_ERROR("Could not allocate MPI addr");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    na_mpi_addr->comm = MPI_COMM_NULL;
    na_mpi_addr->rma_comm = MPI_COMM_NULL;
    na_mpi_addr->rank = 0;
    na_mpi_addr->unexpected = false;
    na_mpi_addr->self = true;
    na_mpi_addr->dynamic = false;
    memset(na_mpi_addr->port_name, '\0', MPI_MAX_PORT_NAME);
    if (!NA_MPI_CLASS(na_class)->use_static_inter_comm &&
        NA_MPI_CLASS(na_class)->listening)
        strcpy(na_mpi_addr->port_name, NA_MPI_CLASS(na_class)->port_name);

    *addr = (na_addr_t *) na_mpi_addr;

done:
    if (ret != NA_SUCCESS) {
        free(na_mpi_addr);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_mpi_addr_free(na_class_t *na_class, na_addr_t *addr)
{
    struct na_mpi_addr *na_mpi_addr = (struct na_mpi_addr *) addr;

    if (na_mpi_addr->self) {
        free(na_mpi_addr);
    } else {
        struct na_mpi_addr *var = NULL;

        /* Remove addr from list of addresses */
        hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->remote_list_mutex);
        LIST_FOREACH (var, &NA_MPI_CLASS(na_class)->remote_list, entry) {
            if (var == na_mpi_addr) {
                LIST_REMOVE(var, entry);
                break;
            }
        }
        hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->remote_list_mutex);

        /* Free addr */
        (void) na_mpi_disconnect(na_class, na_mpi_addr);
    }
}

/*---------------------------------------------------------------------------*/
static bool
na_mpi_addr_cmp(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr1, na_addr_t *addr2)
{
    struct na_mpi_addr *na_mpi_addr1 = (struct na_mpi_addr *) addr1;
    struct na_mpi_addr *na_mpi_addr2 = (struct na_mpi_addr *) addr2;

    return (na_mpi_addr1->comm == na_mpi_addr2->comm) &&
           (na_mpi_addr1->rank == na_mpi_addr2->rank);
}

/*---------------------------------------------------------------------------*/
static bool
na_mpi_addr_is_self(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    struct na_mpi_addr *na_mpi_addr = (struct na_mpi_addr *) addr;

    return na_mpi_addr->self;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr)
{
    struct na_mpi_addr *mpi_addr = NULL;
    size_t string_len;
    char port_name[MPI_MAX_PORT_NAME + 16];
    na_return_t ret = NA_SUCCESS;

    mpi_addr = (struct na_mpi_addr *) addr;

    if (NA_MPI_CLASS(na_class)->use_static_inter_comm) {
        sprintf(port_name, "rank#%d$", mpi_addr->rank);
    } else {
        sprintf(port_name, "%s;rank#%d$", mpi_addr->port_name, mpi_addr->rank);
    }

    string_len = strlen(port_name);
    if (buf) {
        if (string_len >= *buf_size) {
            NA_LOG_ERROR("Buffer size too small to copy addr");
            ret = NA_SIZE_ERROR;
        } else {
            strcpy(buf, port_name);
        }
    }
    *buf_size = string_len + 1;

    return ret;
}

/*---------------------------------------------------------------------------*/
static size_t
na_mpi_msg_get_max_unexpected_size(const na_class_t *na_class)
{
    return NA_MPI_CLASS(na_class)->unexpected_size_max;
}

/*---------------------------------------------------------------------------*/
static size_t
na_mpi_msg_get_max_expected_size(const na_class_t *na_class)
{
    return NA_MPI_CLASS(na_class)->expected_size_max;
}

/*---------------------------------------------------------------------------*/
static na_tag_t
na_mpi_msg_get_max_tag(const na_class_t NA_UNUSED *na_class)
{
    na_tag_t max_tag = (na_tag_t) NA_MPI_MAX_TAG;

    return max_tag;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    int mpi_buf_size = (int) buf_size;
    int mpi_tag = (int) tag;
    struct na_mpi_addr *mpi_addr = (struct na_mpi_addr *) dest_addr;
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    na_mpi_op_id = (struct na_mpi_op_id *) op_id;
    na_mpi_op_id->context = context;
    na_mpi_op_id->type = NA_CB_SEND_UNEXPECTED;
    na_mpi_op_id->callback = callback;
    na_mpi_op_id->arg = arg;
    hg_atomic_set32(&na_mpi_op_id->completed, 0);
    na_mpi_op_id->canceled = false;
    na_mpi_op_id->info.send_unexpected.data_request = MPI_REQUEST_NULL;

    mpi_ret = MPI_Isend(buf, mpi_buf_size, MPI_BYTE, mpi_addr->rank, mpi_tag,
        mpi_addr->comm, &na_mpi_op_id->info.send_unexpected.data_request);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Isend() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Append op_id to op_id list */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    TAILQ_INSERT_TAIL(&NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);

done:
    if (ret != NA_SUCCESS) {
        hg_atomic_set32(&na_mpi_op_id->completed, 1);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_op_id_t *op_id)
{
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Allocate na_op_id */
    na_mpi_op_id = (struct na_mpi_op_id *) op_id;
    na_mpi_op_id->context = context;
    na_mpi_op_id->type = NA_CB_RECV_UNEXPECTED;
    na_mpi_op_id->callback = callback;
    na_mpi_op_id->arg = arg;
    hg_atomic_set32(&na_mpi_op_id->completed, 0);
    na_mpi_op_id->canceled = false;
    na_mpi_op_id->info.recv_unexpected.buf = buf;
    na_mpi_op_id->info.recv_unexpected.buf_size = (int) buf_size;
    na_mpi_op_id->info.recv_unexpected.remote_addr = NULL;

    /* Add op_id to queue of pending unexpected recv ops and make some progress
     * in case messages are already arrived */
    na_mpi_msg_unexpected_op_push(na_class, na_mpi_op_id);

    // do {
    //     ret = na_mpi_progress_unexpected(na_class, context, 0);
    //     if (ret != NA_SUCCESS && ret != NA_TIMEOUT) {
    //         NA_LOG_ERROR("Could not make unexpected progress");
    //         goto done;
    //     }
    // } while (ret == NA_SUCCESS);
    // /* No guarantee here that ours has completed even if progressed is true,
    //  * we make progress here just in case we can complete the op at the same
    //  * time */
    // ret = NA_SUCCESS;

    // done:
    //     if (ret != NA_SUCCESS) {
    //         hg_atomic_set32(&na_mpi_op_id->completed, 1);
    //     }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *dest_addr,
    uint8_t NA_UNUSED dest_id, na_tag_t tag, na_op_id_t *op_id)
{
    int mpi_buf_size = (int) buf_size;
    int mpi_tag = (int) tag;
    struct na_mpi_addr *mpi_addr = (struct na_mpi_addr *) dest_addr;
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    /* Allocate op_id */
    na_mpi_op_id = (struct na_mpi_op_id *) op_id;
    na_mpi_op_id->context = context;
    na_mpi_op_id->type = NA_CB_SEND_EXPECTED;
    na_mpi_op_id->callback = callback;
    na_mpi_op_id->arg = arg;
    hg_atomic_set32(&na_mpi_op_id->completed, 0);
    na_mpi_op_id->canceled = false;
    na_mpi_op_id->info.send_expected.data_request = MPI_REQUEST_NULL;

    mpi_ret = MPI_Isend(buf, mpi_buf_size, MPI_BYTE, mpi_addr->rank, mpi_tag,
        mpi_addr->comm, &na_mpi_op_id->info.send_expected.data_request);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Isend() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Append op_id to op_id list assign op_id */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    TAILQ_INSERT_TAIL(&NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);

done:
    if (ret != NA_SUCCESS) {
        hg_atomic_set32(&na_mpi_op_id->completed, 1);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size,
    void NA_UNUSED *plugin_data, na_addr_t *source_addr,
    uint8_t NA_UNUSED source_id, na_tag_t tag, na_op_id_t *op_id)
{
    int mpi_buf_size = (int) buf_size;
    int mpi_tag = (int) tag;
    struct na_mpi_addr *mpi_addr = (struct na_mpi_addr *) source_addr;
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    /* Allocate op_id */
    na_mpi_op_id = (struct na_mpi_op_id *) op_id;
    na_mpi_op_id->context = context;
    na_mpi_op_id->type = NA_CB_RECV_EXPECTED;
    na_mpi_op_id->callback = callback;
    na_mpi_op_id->arg = arg;
    hg_atomic_set32(&na_mpi_op_id->completed, 0);
    na_mpi_op_id->canceled = false;
    na_mpi_op_id->info.recv_expected.buf_size = mpi_buf_size;
    na_mpi_op_id->info.recv_expected.actual_size = 0;
    na_mpi_op_id->info.recv_expected.data_request = MPI_REQUEST_NULL;

    mpi_ret = MPI_Irecv(buf, mpi_buf_size, MPI_BYTE, mpi_addr->rank, mpi_tag,
        mpi_addr->comm, &na_mpi_op_id->info.recv_expected.data_request);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Irecv() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Append op_id to op_id list */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    TAILQ_INSERT_TAIL(&NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);

done:
    if (ret != NA_SUCCESS) {
        hg_atomic_set32(&na_mpi_op_id->completed, 1);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_mem_handle_create(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, unsigned long flags, na_mem_handle_t **mem_handle)
{
    struct na_mpi_mem_handle *na_mpi_mem_handle = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Allocate memory handle (use calloc to avoid uninitialized transfer) */
    na_mpi_mem_handle = (struct na_mpi_mem_handle *) calloc(
        1, sizeof(struct na_mpi_mem_handle));
    if (!na_mpi_mem_handle) {
        NA_LOG_ERROR("Could not allocate NA MPI memory handle");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    na_mpi_mem_handle->base = buf;
    na_mpi_mem_handle->size = (MPI_Aint) buf_size;
    na_mpi_mem_handle->attr = (uint8_t) flags;

    *mem_handle = (na_mem_handle_t *) na_mpi_mem_handle;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_mpi_mem_handle_free(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    struct na_mpi_mem_handle *mpi_mem_handle =
        (struct na_mpi_mem_handle *) mem_handle;

    free(mpi_mem_handle);
}

/*---------------------------------------------------------------------------*/
static size_t
na_mpi_mem_handle_get_serialize_size(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t NA_UNUSED *mem_handle)
{
    return sizeof(struct na_mpi_mem_handle);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_mem_handle_serialize(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, na_mem_handle_t *mem_handle)
{
    struct na_mpi_mem_handle *na_mpi_mem_handle =
        (struct na_mpi_mem_handle *) mem_handle;
    na_return_t ret = NA_SUCCESS;

    if (buf_size < sizeof(struct na_mpi_mem_handle)) {
        NA_LOG_ERROR("Buffer size too small for serializing handle");
        ret = NA_SIZE_ERROR;
        goto done;
    }

    /* Copy struct */
    memcpy(buf, na_mpi_mem_handle, sizeof(struct na_mpi_mem_handle));

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_mem_handle_deserialize(na_class_t NA_UNUSED *na_class,
    na_mem_handle_t **mem_handle, const void *buf, size_t buf_size)
{
    struct na_mpi_mem_handle *na_mpi_mem_handle = NULL;
    na_return_t ret = NA_SUCCESS;

    if (buf_size < sizeof(struct na_mpi_mem_handle)) {
        NA_LOG_ERROR("Buffer size too small for deserializing handle");
        ret = NA_SIZE_ERROR;
        goto done;
    }

    na_mpi_mem_handle =
        (struct na_mpi_mem_handle *) malloc(sizeof(struct na_mpi_mem_handle));
    if (!na_mpi_mem_handle) {
        NA_LOG_ERROR("Could not allocate NA MPI memory handle");
        ret = NA_NOMEM_ERROR;
        goto done;
    }

    /* Copy struct */
    memcpy(na_mpi_mem_handle, buf, sizeof(struct na_mpi_mem_handle));

    *mem_handle = (na_mem_handle_t *) na_mpi_mem_handle;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    struct na_mpi_mem_handle *mpi_local_mem_handle =
        (struct na_mpi_mem_handle *) local_mem_handle;
    MPI_Aint mpi_local_offset = (MPI_Aint) local_offset;
    struct na_mpi_mem_handle *mpi_remote_mem_handle =
        (struct na_mpi_mem_handle *) remote_mem_handle;
    MPI_Aint mpi_remote_offset = (MPI_Aint) remote_offset;
    struct na_mpi_addr *na_mpi_addr = (struct na_mpi_addr *) remote_addr;
    int mpi_length = (int) length; /* TODO careful here that we don't send more
                                    * than 2GB */
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    struct na_mpi_rma_info *na_mpi_rma_info = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    switch (mpi_remote_mem_handle->attr) {
        case NA_MEM_READ_ONLY:
            NA_LOG_ERROR("Registered memory requires write permission");
            ret = NA_PERMISSION_ERROR;
            goto done;
        case NA_MEM_WRITE_ONLY:
        case NA_MEM_READWRITE:
            break;
        default:
            NA_LOG_ERROR("Invalid memory access flag");
            ret = NA_INVALID_PARAM;
            goto done;
    }

    na_mpi_op_id = (struct na_mpi_op_id *) op_id;
    na_mpi_op_id->context = context;
    na_mpi_op_id->type = NA_CB_PUT;
    na_mpi_op_id->callback = callback;
    na_mpi_op_id->arg = arg;
    hg_atomic_set32(&na_mpi_op_id->completed, 0);
    na_mpi_op_id->canceled = false;
    na_mpi_op_id->info.put.rma_request = MPI_REQUEST_NULL;
    na_mpi_op_id->info.put.data_request = MPI_REQUEST_NULL;
    na_mpi_op_id->info.put.internal_progress = false;
    na_mpi_op_id->info.put.rma_info = NULL;

    /* Allocate rma info (use calloc to avoid uninitialized transfer) */
    na_mpi_rma_info =
        (struct na_mpi_rma_info *) calloc(1, sizeof(struct na_mpi_rma_info));
    if (!na_mpi_rma_info) {
        NA_LOG_ERROR("Could not allocate NA MPI RMA info");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    na_mpi_rma_info->op = NA_MPI_RMA_PUT;
    na_mpi_rma_info->base = mpi_remote_mem_handle->base;
    na_mpi_rma_info->disp = mpi_remote_offset;
    na_mpi_rma_info->count = mpi_length;
    na_mpi_rma_info->tag = na_mpi_gen_rma_tag(na_class);
    na_mpi_op_id->info.put.rma_info = na_mpi_rma_info;

    /* Post the MPI send request */
    mpi_ret = MPI_Isend(na_mpi_rma_info, sizeof(struct na_mpi_rma_info),
        MPI_BYTE, na_mpi_addr->rank, NA_MPI_RMA_REQUEST_TAG,
        na_mpi_addr->rma_comm, &na_mpi_op_id->info.put.rma_request);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Isend() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Simply do a non blocking synchronous send */
    mpi_ret = MPI_Issend((char *) mpi_local_mem_handle->base + mpi_local_offset,
        mpi_length, MPI_BYTE, na_mpi_addr->rank, (int) na_mpi_rma_info->tag,
        na_mpi_addr->rma_comm, &na_mpi_op_id->info.put.data_request);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Issend() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Append op_id to op_id list */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    TAILQ_INSERT_TAIL(&NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);

done:
    if (ret != NA_SUCCESS) {
        free(na_mpi_rma_info);
        hg_atomic_set32(&na_mpi_op_id->completed, 1);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t NA_UNUSED remote_id,
    na_op_id_t *op_id)
{
    struct na_mpi_mem_handle *mpi_local_mem_handle =
        (struct na_mpi_mem_handle *) local_mem_handle;
    MPI_Aint mpi_local_offset = (MPI_Aint) local_offset;
    struct na_mpi_mem_handle *mpi_remote_mem_handle =
        (struct na_mpi_mem_handle *) remote_mem_handle;
    MPI_Aint mpi_remote_offset = (MPI_Aint) remote_offset;
    struct na_mpi_addr *na_mpi_addr = (struct na_mpi_addr *) remote_addr;
    int mpi_length = (int) length; /* TODO careful here that we don't send more
                                    * than 2GB */
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    struct na_mpi_rma_info *na_mpi_rma_info = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    switch (mpi_remote_mem_handle->attr) {
        case NA_MEM_WRITE_ONLY:
            NA_LOG_ERROR("Registered memory requires read permission");
            ret = NA_PERMISSION_ERROR;
            goto done;
        case NA_MEM_READ_ONLY:
        case NA_MEM_READWRITE:
            break;
        default:
            NA_LOG_ERROR("Invalid memory access flag");
            ret = NA_INVALID_PARAM;
            goto done;
    }

    na_mpi_op_id = (struct na_mpi_op_id *) op_id;
    na_mpi_op_id->context = context;
    na_mpi_op_id->type = NA_CB_GET;
    na_mpi_op_id->callback = callback;
    na_mpi_op_id->arg = arg;
    hg_atomic_set32(&na_mpi_op_id->completed, 0);
    na_mpi_op_id->canceled = false;
    na_mpi_op_id->info.get.rma_request = MPI_REQUEST_NULL;
    na_mpi_op_id->info.get.data_request = MPI_REQUEST_NULL;
    na_mpi_op_id->info.put.internal_progress = false;
    na_mpi_op_id->info.get.rma_info = NULL;

    /* Allocate rma info (use calloc to avoid uninitialized transfer) */
    na_mpi_rma_info =
        (struct na_mpi_rma_info *) calloc(1, sizeof(struct na_mpi_rma_info));
    if (!na_mpi_rma_info) {
        NA_LOG_ERROR("Could not allocate NA MPI RMA info");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    na_mpi_rma_info->op = NA_MPI_RMA_GET;
    na_mpi_rma_info->base = mpi_remote_mem_handle->base;
    na_mpi_rma_info->disp = mpi_remote_offset;
    na_mpi_rma_info->count = mpi_length;
    na_mpi_rma_info->tag = na_mpi_gen_rma_tag(na_class);
    na_mpi_op_id->info.get.rma_info = na_mpi_rma_info;

    /* Post the MPI send request */
    mpi_ret = MPI_Isend(na_mpi_rma_info, sizeof(struct na_mpi_rma_info),
        MPI_BYTE, na_mpi_addr->rank, NA_MPI_RMA_REQUEST_TAG,
        na_mpi_addr->rma_comm, &na_mpi_op_id->info.get.rma_request);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Isend() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Simply do an asynchronous recv */
    mpi_ret = MPI_Irecv((char *) mpi_local_mem_handle->base + mpi_local_offset,
        mpi_length, MPI_BYTE, na_mpi_addr->rank, (int) na_mpi_rma_info->tag,
        na_mpi_addr->rma_comm, &na_mpi_op_id->info.get.data_request);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Irecv() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Append op_id to op_id list */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    TAILQ_INSERT_TAIL(&NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);

done:
    if (ret != NA_SUCCESS) {
        free(na_mpi_rma_info);
        hg_atomic_set32(&na_mpi_op_id->completed, 1);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p)
{
    unsigned int count = 0;
    na_return_t ret;

    /* Try to make unexpected progress */
    ret = na_mpi_progress_unexpected(na_class, context, 0);
    if (ret != NA_SUCCESS) {
        if (ret != NA_TIMEOUT) {
            NA_LOG_ERROR("Could not make unexpected progress");
            goto error;
        }
    } else
        count++; /* Progressed */

    /* Try to make expected progress */
    ret = na_mpi_progress_expected(na_class, context, 0);
    if (ret != NA_SUCCESS) {
        if (ret != NA_TIMEOUT) {
            NA_LOG_ERROR("Could not make expected progress");
            goto error;
        }
    } else
        count++; /* Progressed */

    if (count_p != NULL)
        *count_p = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_progress_unexpected(
    na_class_t *na_class, na_context_t *context, unsigned int NA_UNUSED timeout)
{
    struct na_mpi_addr *probe_addr = NULL;
    na_return_t ret = NA_TIMEOUT;
    int mpi_ret;

    /* Process list of communicators */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->remote_list_mutex);

    LIST_FOREACH (probe_addr, &NA_MPI_CLASS(na_class)->remote_list, entry) {
        MPI_Status status1, status2;
        int flag = 0;

        /* First look for user unexpected message */
        mpi_ret = MPI_Iprobe(
            MPI_ANY_SOURCE, MPI_ANY_TAG, probe_addr->comm, &flag, &status1);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("MPI_Iprobe() failed");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }

        if (flag) {
            ret = na_mpi_progress_unexpected_msg(
                na_class, context, probe_addr, &status1);
            if (ret != NA_SUCCESS) {
                if (ret != NA_TIMEOUT) {
                    NA_LOG_ERROR("Could not make unexpected MSG progress");
                    goto done;
                }
            } else
                break; /* Progressed */
        }

        /* Look for internal unexpected RMA requests */
        mpi_ret = MPI_Iprobe(probe_addr->rank, NA_MPI_RMA_REQUEST_TAG,
            probe_addr->rma_comm, &flag, &status2);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("MPI_Iprobe() failed");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }

        if (flag) {
            ret = na_mpi_progress_unexpected_rma(
                na_class, context, probe_addr, &status2);
            if (ret != NA_SUCCESS) {
                NA_LOG_ERROR("Could not make unexpected RMA progress");
                goto done;
            } else
                break; /* Progressed */
        }
    }

done:
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->remote_list_mutex);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_progress_unexpected_msg(na_class_t *na_class,
    na_context_t NA_UNUSED *context, struct na_mpi_addr *na_mpi_addr,
    const MPI_Status *status)
{
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    int unexpected_buf_size = 0;
    na_return_t ret = NA_TIMEOUT;
    int mpi_ret;

    MPI_Get_count(status, MPI_BYTE, &unexpected_buf_size);
    if (unexpected_buf_size >
        (int) na_mpi_msg_get_max_unexpected_size(na_class)) {
        NA_LOG_ERROR("Exceeding unexpected MSG size");
        ret = NA_SIZE_ERROR;
        goto done;
    }

    /* Try to pop an unexpected recv op id */
    na_mpi_op_id = na_mpi_msg_unexpected_op_pop(na_class);
    if (!na_mpi_op_id) {
        /* Can't process it since nobody has posted an unexpected recv yet */
        goto done;
    }

    mpi_ret = MPI_Recv(na_mpi_op_id->info.recv_unexpected.buf,
        na_mpi_op_id->info.recv_unexpected.buf_size, MPI_BYTE,
        status->MPI_SOURCE, status->MPI_TAG, na_mpi_addr->comm,
        MPI_STATUS_IGNORE);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Recv() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    na_mpi_op_id->info.recv_unexpected.remote_addr = na_mpi_addr;
    memcpy(
        &na_mpi_op_id->info.recv_unexpected.status, status, sizeof(MPI_Status));
    ret = na_mpi_complete(na_mpi_op_id);
    if (ret != NA_SUCCESS) {
        NA_LOG_ERROR("Could not complete op id");
        goto done;
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_progress_unexpected_rma(na_class_t *na_class, na_context_t *context,
    struct na_mpi_addr *na_mpi_addr, const MPI_Status *status)
{
    struct na_mpi_rma_info *na_mpi_rma_info = NULL;
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    int unexpected_buf_size = 0;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    MPI_Get_count(status, MPI_BYTE, &unexpected_buf_size);
    if (unexpected_buf_size != sizeof(struct na_mpi_rma_info)) {
        NA_LOG_ERROR("Unexpected message size does not match RMA info struct");
        ret = NA_SIZE_ERROR;
        goto done;
    }

    /* Allocate rma info */
    na_mpi_rma_info =
        (struct na_mpi_rma_info *) malloc(sizeof(struct na_mpi_rma_info));
    if (!na_mpi_rma_info) {
        NA_LOG_ERROR("Could not allocate NA MPI RMA info");
        ret = NA_NOMEM_ERROR;
        goto done;
    }

    /* Recv message (already arrived) */
    mpi_ret = MPI_Recv(na_mpi_rma_info, sizeof(struct na_mpi_rma_info),
        MPI_BYTE, status->MPI_SOURCE, status->MPI_TAG, na_mpi_addr->rma_comm,
        MPI_STATUS_IGNORE);
    if (mpi_ret != MPI_SUCCESS) {
        NA_LOG_ERROR("MPI_Recv() failed");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* Allocate na_op_id */
    na_mpi_op_id = (struct na_mpi_op_id *) na_mpi_op_create(na_class, 0);
    if (!na_mpi_op_id) {
        NA_LOG_ERROR("Could not allocate NA MPI operation ID");
        ret = NA_NOMEM_ERROR;
        goto done;
    }
    /* This is an internal operation so no user callback/arg */
    na_mpi_op_id->context = context;
    na_mpi_op_id->callback = NULL;
    na_mpi_op_id->arg = NULL;
    hg_atomic_set32(&na_mpi_op_id->completed, 0);
    na_mpi_op_id->canceled = false;

    switch (na_mpi_rma_info->op) {
        /* Remote wants to do a put so wait in a recv */
        case NA_MPI_RMA_PUT:
            na_mpi_op_id->type = NA_CB_PUT;
            na_mpi_op_id->info.put.rma_request = MPI_REQUEST_NULL;
            na_mpi_op_id->info.put.data_request = MPI_REQUEST_NULL;
            na_mpi_op_id->info.put.internal_progress = true;
            na_mpi_op_id->info.put.rma_info = na_mpi_rma_info;

            mpi_ret = MPI_Irecv(
                (char *) na_mpi_rma_info->base + na_mpi_rma_info->disp,
                na_mpi_rma_info->count, MPI_BYTE, status->MPI_SOURCE,
                (int) na_mpi_rma_info->tag, na_mpi_addr->rma_comm,
                &na_mpi_op_id->info.put.data_request);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Irecv() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
            break;

            /* Remote wants to do a get so do a send */
        case NA_MPI_RMA_GET:
            na_mpi_op_id->type = NA_CB_GET;
            na_mpi_op_id->info.get.rma_request = MPI_REQUEST_NULL;
            na_mpi_op_id->info.get.data_request = MPI_REQUEST_NULL;
            na_mpi_op_id->info.get.internal_progress = true;
            na_mpi_op_id->info.get.rma_info = na_mpi_rma_info;

            mpi_ret = MPI_Isend(
                (char *) na_mpi_rma_info->base + na_mpi_rma_info->disp,
                na_mpi_rma_info->count, MPI_BYTE, status->MPI_SOURCE,
                (int) na_mpi_rma_info->tag, na_mpi_addr->rma_comm,
                &na_mpi_op_id->info.get.data_request);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Isend() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
            break;

        default:
            NA_LOG_ERROR("Operation not supported");
            break;
    }

    /* Add op_id to list */
    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    TAILQ_INSERT_TAIL(&NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);

done:
    if (ret != NA_SUCCESS) {
        free(na_mpi_rma_info);
        hg_atomic_set32(&na_mpi_op_id->completed, 1);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_progress_expected(na_class_t *na_class, na_context_t NA_UNUSED *context,
    unsigned int NA_UNUSED timeout)
{
    struct na_mpi_op_id *na_mpi_op_id = NULL;
    na_return_t ret = NA_TIMEOUT;

    hg_thread_mutex_lock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);

    na_mpi_op_id = TAILQ_FIRST(&NA_MPI_CLASS(na_class)->op_id_list);
    while (na_mpi_op_id) {
        MPI_Request *request = NULL;
        bool internal = false; /* Only used to complete internal ops */
        struct na_mpi_rma_info **rma_info = NULL;
        bool complete_op_id = true;
        int flag = 0, mpi_ret = 0;
        MPI_Status *status = MPI_STATUS_IGNORE;

        /* If the op_id is marked as completed, something is wrong */
        if (hg_atomic_get32(&na_mpi_op_id->completed)) {
            NA_LOG_ERROR("Op ID should not have completed yet");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }

        switch (na_mpi_op_id->type) {
            case NA_CB_RECV_UNEXPECTED:
                NA_LOG_ERROR("Should not complete unexpected recv here");
                break;
            case NA_CB_SEND_UNEXPECTED:
                request = &na_mpi_op_id->info.send_unexpected.data_request;
                break;
            case NA_CB_RECV_EXPECTED:
                status = &na_mpi_op_id->info.recv_expected.status;
                request = &na_mpi_op_id->info.recv_expected.data_request;
                break;
            case NA_CB_SEND_EXPECTED:
                request = &na_mpi_op_id->info.send_expected.data_request;
                break;
            case NA_CB_PUT:
                if (na_mpi_op_id->info.put.internal_progress) {
                    request = &na_mpi_op_id->info.put.data_request;
                    rma_info = &na_mpi_op_id->info.put.rma_info;
                    internal = true;
                } else {
                    request = &na_mpi_op_id->info.put.rma_request;
                    if (*request != MPI_REQUEST_NULL) {
                        complete_op_id = false;
                    } else {
                        request = &na_mpi_op_id->info.put.data_request;
                    }
                }
                break;
            case NA_CB_GET:
                if (na_mpi_op_id->info.get.internal_progress) {
                    request = &na_mpi_op_id->info.get.data_request;
                    rma_info = &na_mpi_op_id->info.get.rma_info;
                    internal = true;
                } else {
                    request = &na_mpi_op_id->info.get.rma_request;
                    if (*request != MPI_REQUEST_NULL) {
                        complete_op_id = false;
                    } else {
                        request = &na_mpi_op_id->info.get.data_request;
                    }
                }
                break;
            default:
                NA_LOG_ERROR("Unknown type of operation ID");
                ret = NA_PROTOCOL_ERROR;
                goto done;
        }

        /* If request is MPI_REQUEST_NULL, the operation should be completed */
        if (!request || (request && (*request == MPI_REQUEST_NULL))) {
            NA_LOG_ERROR("NULL request found");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }

        mpi_ret = MPI_Test(request, &flag, status);
        if (mpi_ret != MPI_SUCCESS) {
            NA_LOG_ERROR("MPI_Test() failed");
            ret = NA_PROTOCOL_ERROR;
            goto done;
        }
        if (!flag) {
            na_mpi_op_id = TAILQ_NEXT(na_mpi_op_id, entry);
            continue;
        }

        *request = MPI_REQUEST_NULL;

        /* If internal operation call release directly otherwise add callback
         * to completion queue */
        if (internal) {
            hg_atomic_set32(&na_mpi_op_id->completed, 1);
            /* Remove entry from list */
            TAILQ_REMOVE(
                &NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);

            free(*rma_info);
            *rma_info = NULL;
            na_mpi_op_destroy(na_class, (na_op_id_t *) na_mpi_op_id);
        } else {
            if (!complete_op_id) {
                na_mpi_op_id = TAILQ_NEXT(na_mpi_op_id, entry);
                continue;
            }
            /* Remove entry from list */
            TAILQ_REMOVE(
                &NA_MPI_CLASS(na_class)->op_id_list, na_mpi_op_id, entry);

            ret = na_mpi_complete(na_mpi_op_id);
            if (ret != NA_SUCCESS) {
                NA_LOG_ERROR("Could not complete operation");
                goto done;
            }
        }
        ret = NA_SUCCESS; /* progressed */
        break;
    }

done:
    hg_thread_mutex_unlock(&NA_MPI_CLASS(na_class)->op_id_list_mutex);
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_complete(struct na_mpi_op_id *na_mpi_op_id)
{
    struct na_cb_info *callback_info = NULL;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    /* Mark op id as completed */
    hg_atomic_set32(&na_mpi_op_id->completed, 1);

    /* Init callback info */
    callback_info = &na_mpi_op_id->completion_data.callback_info;
    callback_info->arg = na_mpi_op_id->arg;
    callback_info->ret = (na_mpi_op_id->canceled) ? NA_CANCELED : ret;
    callback_info->type = na_mpi_op_id->type;

    switch (na_mpi_op_id->type) {
        case NA_CB_SEND_UNEXPECTED:
            break;
        case NA_CB_RECV_UNEXPECTED: {
            struct na_mpi_addr *na_mpi_addr = NULL;
            struct na_mpi_addr *na_mpi_remote_addr = NULL;
            MPI_Status *status;
            int recv_size;

            na_mpi_remote_addr = na_mpi_op_id->info.recv_unexpected.remote_addr;
            status = &na_mpi_op_id->info.recv_unexpected.status;

            if (!na_mpi_remote_addr) {
                /* In case of cancellation where no recv'd data */
                callback_info->info.recv_unexpected.actual_buf_size = 0;
                callback_info->info.recv_unexpected.source = NULL;
                callback_info->info.recv_unexpected.tag = 0;
                break;
            }

            /* Check count */
            mpi_ret = MPI_Get_count(status, MPI_BYTE, &recv_size);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Get_count() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }

            /* Allocate addr */
            na_mpi_addr =
                (struct na_mpi_addr *) malloc(sizeof(struct na_mpi_addr));
            if (!na_mpi_addr) {
                NA_LOG_ERROR("Could not allocate MPI addr");
                ret = NA_NOMEM_ERROR;
                goto done;
            }
            na_mpi_addr->comm = na_mpi_remote_addr->comm;
            na_mpi_addr->rma_comm = na_mpi_remote_addr->rma_comm;
            na_mpi_addr->rank = status->MPI_SOURCE;
            na_mpi_addr->unexpected = true;
            na_mpi_addr->self = false;
            na_mpi_addr->dynamic = true;
            memset(na_mpi_addr->port_name, '\0', MPI_MAX_PORT_NAME);
            /* Can only write debug info here */
            sprintf(na_mpi_addr->port_name, "comm: %d rank:%d\n",
                (int) na_mpi_addr->comm, na_mpi_addr->rank);

            /* Fill callback info */
            callback_info->info.recv_unexpected.actual_buf_size =
                (size_t) recv_size;
            callback_info->info.recv_unexpected.source =
                (na_addr_t *) na_mpi_addr;
            callback_info->info.recv_unexpected.tag =
                (na_tag_t) status->MPI_TAG;
        } break;
        case NA_CB_SEND_EXPECTED:
            break;
        case NA_CB_RECV_EXPECTED:
            /* Check buf_size and actual_size */
            mpi_ret = MPI_Get_count(&na_mpi_op_id->info.recv_expected.status,
                MPI_BYTE, &na_mpi_op_id->info.recv_expected.actual_size);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Get_count() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
            if (na_mpi_op_id->info.recv_expected.actual_size >
                na_mpi_op_id->info.recv_expected.buf_size) {
                NA_LOG_ERROR("Expected recv size too large for buffer");
                ret = NA_SIZE_ERROR;
                goto done;
            }
            callback_info->info.recv_expected.actual_buf_size =
                (size_t) na_mpi_op_id->info.recv_expected.actual_size;
            break;
        case NA_CB_PUT:
            /* Transfer is now done so free RMA info */
            free(na_mpi_op_id->info.put.rma_info);
            na_mpi_op_id->info.put.rma_info = NULL;
            break;
        case NA_CB_GET:
            /* Transfer is now done so free RMA info */
            free(na_mpi_op_id->info.get.rma_info);
            na_mpi_op_id->info.get.rma_info = NULL;
            break;
        default:
            NA_LOG_ERROR("Operation not supported");
            ret = NA_INVALID_PARAM;
            break;
    }

    na_mpi_op_id->completion_data.callback = na_mpi_op_id->callback;
    na_mpi_op_id->completion_data.plugin_callback = na_mpi_release;
    na_mpi_op_id->completion_data.plugin_callback_args = na_mpi_op_id;

    na_cb_completion_add(na_mpi_op_id->context, &na_mpi_op_id->completion_data);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_mpi_release(void *arg)
{
    struct na_mpi_op_id *na_mpi_op_id = (struct na_mpi_op_id *) arg;

    if (na_mpi_op_id && !hg_atomic_get32(&na_mpi_op_id->completed)) {
        NA_LOG_WARNING("Releasing resources from an uncompleted operation");
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_mpi_cancel(
    na_class_t *na_class, na_context_t NA_UNUSED *context, na_op_id_t *op_id)
{
    struct na_mpi_op_id *na_mpi_op_id = (struct na_mpi_op_id *) op_id;
    na_return_t ret = NA_SUCCESS;
    int mpi_ret;

    if (hg_atomic_get32(&na_mpi_op_id->completed))
        goto done;

    switch (na_mpi_op_id->type) {
        case NA_CB_SEND_UNEXPECTED:
            mpi_ret =
                MPI_Cancel(&na_mpi_op_id->info.send_unexpected.data_request);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Cancel() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
            na_mpi_op_id->canceled = true;
            break;
        case NA_CB_RECV_UNEXPECTED: {
            struct na_mpi_op_id *na_mpi_pop_op_id = NULL;

            /* Must remove op_id from unexpected op_id queue */
            while (na_mpi_pop_op_id != na_mpi_op_id) {
                na_mpi_pop_op_id = na_mpi_msg_unexpected_op_pop(na_class);

                /* Push back unexpected op_id to queue if it does not match */
                if (na_mpi_pop_op_id != na_mpi_op_id) {
                    na_mpi_msg_unexpected_op_push(na_class, na_mpi_pop_op_id);
                } else {
                    na_mpi_op_id->canceled = true;
                    ret = na_mpi_complete(na_mpi_op_id);
                    if (ret != NA_SUCCESS) {
                        NA_LOG_ERROR("Could not complete op id");
                        goto done;
                    }
                }
            }
        } break;
        case NA_CB_SEND_EXPECTED:
            mpi_ret =
                MPI_Cancel(&na_mpi_op_id->info.send_expected.data_request);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Cancel() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
            na_mpi_op_id->canceled = true;
            break;
        case NA_CB_RECV_EXPECTED:
            mpi_ret =
                MPI_Cancel(&na_mpi_op_id->info.recv_expected.data_request);
            if (mpi_ret != MPI_SUCCESS) {
                NA_LOG_ERROR("MPI_Cancel() failed");
                ret = NA_PROTOCOL_ERROR;
                goto done;
            }
            na_mpi_op_id->canceled = true;
            break;
        case NA_CB_PUT:
            /* TODO */
            break;
        case NA_CB_GET:
            /* TODO */
            break;
        default:
            NA_LOG_ERROR("Operation not supported");
            ret = NA_INVALID_PARAM;
            break;
    }

done:
    return ret;
}
