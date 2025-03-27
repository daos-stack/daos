/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_BUILTIN_OPS_H_
#define UCG_BUILTIN_OPS_H_

#include "../plan/builtin_plan.h"
#include <ucp/core/ucp_request.h>
#include <ucs/datastruct/ptr_array.h>

BEGIN_C_DECLS

/*
 * The built-in collective operations are composed of one or more steps.
 * In each step, we apply a method to a subgroup of peer processes.
 * Collectives are planned using "templates", and once the user
 * provides the details a step is "instantiated" from a suitable
 * template and the instance is executed. Often more than one instance
 * is created from the same template, and instances can run side-by-side.
 *
 * Methods are the basic algorithmic building blocks, like fan-in and
 * fan-out for trees, or the "Recursive K-ing" algorithm.
 * For example, Allreduce can either be done in two step,
 * fan-in and fanout, or in a single Recursive K-ing step.
 * Once the user requests an Allreduce operation - the selected
 * step templates are used to generate an instance
 * (or it is fetched from cache) and that instance is executed.
 */

typedef void (*mpi_reduce_f)(void *mpi_op, char *src_buffer,
                             char *dst_buffer, unsigned dcount,
                             void* mpi_datatype);

extern ucg_plan_component_t ucg_builtin_component;
extern mpi_reduce_f ucg_builtin_mpi_reduce_cb;
extern unsigned builtin_base_am_id;

typedef union ucg_builtin_header_step {
    struct {
        ucg_coll_id_t  coll_id;
        ucg_step_idx_t step_idx;
    };
    uint16_t local_id;
} ucg_builtin_header_step_t;

typedef union ucg_builtin_header {
    struct {
        ucg_group_id_t group_id;
        ucg_builtin_header_step_t msg;
        ucg_offset_t remote_offset;
    };
    uint64_t header;
} ucg_builtin_header_t;

/*
 * The builtin operation
 */
enum ucg_builtin_op_step_flags {
    /* General characteristics */
    UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND    = UCS_BIT(0),
    UCG_BUILTIN_OP_STEP_FLAG_RECV_BEFORE_SEND1  = UCS_BIT(1),
    UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND  = UCS_BIT(2),

    UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP         = UCS_BIT(3),
    UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP          = UCS_BIT(4),
    UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT    = UCS_BIT(5),
    UCG_BUILTIN_OP_STEP_FLAG_CALC_SENT_BUFFERS  = UCS_BIT(6),
    UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED         = UCS_BIT(7),
    UCG_BUILTIN_OP_STEP_FLAG_PIPELINED          = UCS_BIT(8),
    UCG_BUILTIN_OP_STEP_FLAG_LOCKED_PACK_CB     = UCS_BIT(9),
    UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF = UCS_BIT(10),

    /* Send types */
    UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT      = UCS_BIT(11),
    UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY      = UCS_BIT(12),
    UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY      = UCS_BIT(13),
};

/* Definitions of several callback functions, used during an operation */
typedef struct ucg_builtin_op ucg_builtin_op_t;
typedef struct ucg_builtin_request ucg_builtin_request_t;
typedef void         (*ucg_builtin_op_init_cb_t)  (ucg_builtin_op_t *op,
                                                   ucg_coll_id_t coll_id);
typedef void         (*ucg_builtin_op_fini_cb_t)  (ucg_builtin_op_t *op,
                                                   ucg_coll_id_t coll_id);
typedef ucs_status_t (*ucg_builtin_op_optm_cb_t)  (ucg_builtin_op_t *op);
typedef void         (*ucg_builtin_step_calc_cb_t)(ucg_builtin_request_t *req,
                                                   uint8_t *send_count,
                                                   size_t *base_offset,
                                                   size_t *item_interval);
typedef int          (*ucg_builtin_comp_recv_cb_t)(ucg_builtin_request_t *req,
                                                   uint64_t offset,
                                                   void *data,
                                                   size_t length);

typedef struct ucg_builtin_zcomp {
    uct_completion_t           comp;
    ucg_builtin_request_t     *req;
} ucg_builtin_zcomp_t;

typedef struct ucg_builtin_op_step {
    uint16_t                   flags;            /* @ref enum ucg_builtin_op_step_flags */
    uint8_t                    iter_ep;          /* iterator, somewhat volatile */
    uint8_t                    iter_calc;        /* iterator, somewhat volatile */
    ucg_offset_t               iter_offset;      /* iterator, somewhat volatile */
#define UCG_BUILTIN_OFFSET_PIPELINE_READY   ((ucg_offset_t)-1)
#define UCG_BUILTIN_OFFSET_PIPELINE_PENDING ((ucg_offset_t)-2)

    uct_iface_h                uct_iface;
    uct_md_h                   uct_md;
    ucg_builtin_plan_phase_t  *phase;

    int8_t                    *send_buffer;
    int8_t                    *recv_buffer;
    size_t                     buffer_length;
    ucg_builtin_header_t       am_header;
    uint16_t                   batch_cnt;
    uint8_t                    am_id;

    uint32_t                   fragments;        /* != 1 for fragmented operations */
    size_t                     fragment_length;  /* only for fragmented operations */
    /* To enable pipelining of fragmented messages, each fragment has a counter,
     * similar to the request's overall "pending" counter. Once it reaches zero,
     * the fragment can be "forwarded" regardless of the other fragments.
     * This optimization is only valid for "*_WAYPOINT" methods. */
#define UCG_BUILTIN_FRAG_PENDING ((uint8_t)-1)
    volatile uint8_t          *fragment_pending;

    /* Step-level callback functions (as opposed to Op-level callback functions) */
    ucg_builtin_step_calc_cb_t calc_cb;
    ucg_builtin_comp_recv_cb_t recv_cb;

    /* Fields intended for zero-copy */
    struct {
        uct_mem_h              memh;
        ucg_builtin_zcomp_t   *zcomp;
    } zcopy;
} ucg_builtin_op_step_t;

typedef struct ucg_builtin_comp_slot ucg_builtin_comp_slot_t;
struct ucg_builtin_op {
    ucg_op_t                 super;
    unsigned                 opt_cnt; /**< optimization count-down */
    ucg_builtin_op_optm_cb_t optm_cb; /**< optimization function for the operation */
    ucg_builtin_op_init_cb_t init_cb; /**< Initialization function for the operation */
    ucg_builtin_op_fini_cb_t fini_cb; /**< Finalization function for the operation */
    ucg_builtin_comp_slot_t *slots;   /**< slots pointer, for faster initialization */
    ucg_builtin_op_step_t    steps[]; /**< steps required to complete the operation */
};

/*
 * For every instance of the builtin collective operation (op), we create allocate
 * a request to handle completion and interaction with the user (via API).
 */
struct ucg_builtin_request {
    ucg_request_t             super;
    ucg_builtin_op_step_t    *step;      /**< indicator of current step within the op */
    ucg_builtin_op_t         *op;        /**< operation currently running */
    ucg_request_t            *comp_req;  /**< completion status is written here */
    volatile uint32_t         pending;   /**< number of step's pending messages */
    ucg_builtin_header_step_t latest;    /**< request iterator, mostly here for
                                              alignment reasons with slot structs */
};

ucs_status_t ucg_builtin_step_create (ucg_builtin_plan_phase_t *phase,
                                      unsigned extra_flags,
                                      unsigned base_am_id,
                                      ucg_group_id_t group_id,
                                      const ucg_collective_params_t *params,
                                      int8_t **current_data_buffer,
                                      ucg_builtin_op_step_t *step);
ucs_status_t ucg_builtin_step_execute(ucg_builtin_request_t *req,
                                      ucg_request_t **user_req);
#define NO_INCAST_SUPPORT ((size_t)-1)
ucs_status_t ucg_builtin_step_select_callbacks(ucg_builtin_plan_phase_t *phase,
                                               ucg_builtin_comp_recv_cb_t *recv_cb,
                                               int flags, size_t align_incast,
                                               int nonzero_length);
ucs_status_t ucg_builtin_op_select_callback(ucg_builtin_plan_t *plan,
                                            ucg_builtin_op_init_cb_t *init_cb);
ucs_status_t ucg_builtin_step_zcopy_prep(ucg_builtin_op_step_t *step);
ucs_status_t ucg_builtin_op_consider_optimization(ucg_builtin_op_t *op,
                                                  ucg_builtin_config_t *config);

ucs_status_t ucg_builtin_op_create (ucg_plan_t *plan,
                                    const ucg_collective_params_t *params,
                                    ucg_op_t **op);
void         ucg_builtin_op_discard(ucg_op_t *op);
ucs_status_t ucg_builtin_op_trigger(ucg_op_t *op,
                                    ucg_coll_id_t coll_id,
                                    ucg_request_t **request);

/*
 * Macros to generate the headers of all bcopy packing callback functions.
 */
typedef ssize_t (*packed_send_t)(uct_ep_h, uint8_t, uct_pack_locked_callback_t, void*, unsigned);

#define UCG_BUILTIN_PACKER_NAME(_modifier, _mode, _buffer) \
    ucg_builtin_step_am_bcopy_pack ## _modifier ## _mode ## _buffer

#ifdef HAVE_UCP_EXTENSIONS
#define UCG_BUILTIN_PACKER_DECLARE(_modifier, _mode, _buffer) \
    size_t UCG_BUILTIN_PACKER_NAME(_modifier, _mode, _buffer) \
        (void *dest, ucs_spinlock_pure_t *lock, void *arg)
#else
#define UCG_BUILTIN_PACKER_DECLARE(_modifier, _mode, _buffer) \
    size_t UCG_BUILTIN_PACKER_NAME(_modifier, _mode, _buffer) \
        (void *dest, void *arg)
#endif

#define UCG_BUILTIN_PACKER_DECLARE_BY_BUFFER(_modifier, _mode) \
        UCG_BUILTIN_PACKER_DECLARE(_modifier, _mode, _sbuf); \
        UCG_BUILTIN_PACKER_DECLARE(_modifier, _mode, _rbuf);

#define UCG_BUILTIN_PACKER_DECLARE_BY_MODE(_modifier) \
        UCG_BUILTIN_PACKER_DECLARE_BY_BUFFER(_modifier, _single) \
        UCG_BUILTIN_PACKER_DECLARE_BY_BUFFER(_modifier, _full) \
        UCG_BUILTIN_PACKER_DECLARE_BY_BUFFER(_modifier, _partial)

UCG_BUILTIN_PACKER_DECLARE_BY_MODE(_)
UCG_BUILTIN_PACKER_DECLARE_BY_MODE(_locked)
UCG_BUILTIN_PACKER_DECLARE_BY_MODE(_batched)

/*
 * Incoming messages are processed for one of the collective operations
 * currently outstanding - arranged as a window (think: TCP) of slots.
 */
struct ucg_builtin_comp_slot {
    ucg_builtin_request_t      req;
    ucg_builtin_comp_recv_cb_t cb;
    ucs_ptr_array_t            messages;
};

/*
 * This number sets the number of slots available for collective operations.
 * Each operation occupies a slot, so no more than this number of collectives
 * can take place at the same time. The slot is determined by the collective
 * operation id (ucg_coll_id_t) - modulo this constant. Translating "coll_id"
 * to slot# happens on every incoming packet, so this constant is best kept
 * determinable at compile time, and set to a power of 2 (<= 64, to fit into
 * the resend bit-field).
 */
#define UCG_BUILTIN_MAX_CONCURRENT_OPS (16)

END_C_DECLS

#endif
