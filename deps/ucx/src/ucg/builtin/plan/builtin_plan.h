/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCG_BUILTIN_PLAN_H
#define UCG_BUILTIN_PLAN_H

#include <ucg/api/ucg_plan_component.h>
#include <ucs/datastruct/mpool.inl>
#include <ucp/core/ucp_types.h> /* for ucp_rsc_index_t */
#include <uct/api/uct.h>

enum ucg_builtin_plan_topology_type {
    UCG_PLAN_RECURSIVE,
    UCG_PLAN_TREE_FANIN,
    UCG_PLAN_TREE_FANOUT,
    UCG_PLAN_TREE_FANIN_FANOUT,
    UCG_PLAN_ALLTOALL_BRUCK,
    UCG_PLAN_PAIRWISE
} UCS_S_PACKED;

typedef struct ucg_builtin_plan_topology {
    enum ucg_builtin_plan_topology_type type;
    ucg_plan_resources_t *resources;
} ucg_builtin_plan_topology_t;

enum ucg_builtin_plan_method_type {
    UCG_PLAN_METHOD_SEND_TERMINAL,     /* Send the message(s), nothing fancy */
    UCG_PLAN_METHOD_RECV_TERMINAL,     /* Final stop for incoming messages */
    UCG_PLAN_METHOD_BCAST_WAYPOINT,    /* receive and send on to all peers */
    UCG_PLAN_METHOD_GATHER_WAYPOINT,   /* gather from all peers, and pass on */
    UCG_PLAN_METHOD_SCATTER_TERMINAL,  /* scatter to all peers in the map */
    UCG_PLAN_METHOD_SCATTER_WAYPOINT,  /* scatter and send "downwards" */
    UCG_PLAN_METHOD_REDUCE_TERMINAL,   /* receive and reduce from each peer */
    UCG_PLAN_METHOD_REDUCE_WAYPOINT,   /* receive, reduce, and pass onwards */
    UCG_PLAN_METHOD_REDUCE_RECURSIVE,  /* send+receive and reduce (RD) */
    UCG_PLAN_METHOD_ALLTOALL_BRUCK,    /* Send+receive and exchange (all2all) */
    UCG_PLAN_METHOD_ALLGATHER_BRUCK,   /* Send+receive and exchange (all2all) */
    UCG_PLAN_METHOD_PAIRWISE,          /* Both send to and receive from a set */
    UCG_PLAN_METHOD_NEIGHBOR           /* "halo exchange", for neighborhood ops */
} UCS_S_PACKED;

typedef struct ucg_builtin_plan_phase {
    /* Parameters for buffer send/recv action */
    union {
        uct_ep_h                     *multi_eps;     /* endpoint pointer array */
        uct_ep_h                      single_ep;     /* single endpoint handle */
    };
    uint64_t                         *resends;       /* (per-group) step resend bitfield */
    uint16_t                          ep_cnt;        /* Number of endpoints (below) */
    uint16_t                          host_proc_cnt; /* Number of members per host */
    ucg_step_idx_t                    step_index;    /* determines step index */
    /* Until this point - also used during step execution ("data path") */

    /* From here on - only used during step creation ("control path") */
    enum ucg_builtin_plan_method_type method;        /* how to apply this map */
    size_t                            max_short_one; /* max single short message */
    size_t                            max_short_max; /* max length to use short */
    size_t                            max_bcopy_one; /* max single bcopy message */
    size_t                            max_bcopy_max; /* max length to use bcopy */
    size_t                            max_zcopy_one; /* max single zcopy message */

    uct_md_h                          md;            /* memory (registration) domain */
    const uct_md_attr_t              *md_attr;       /* memory domain attributes */
    const uct_iface_attr_t           *ep_attr;       /* endpoint attributes */

#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
    ucg_group_member_index_t         *indexes;       /* array corresponding to EPs */
#define UCG_GROUP_MEMBER_INDEX_UNSPECIFIED ((ucg_group_member_index_t)-1)
#endif
} ucg_builtin_plan_phase_t;

typedef struct ucg_builtin_group_ctx ucg_builtin_group_ctx_t;
typedef struct ucg_builtin_plan {
    ucg_plan_t               super;
    void                    *slots;   /* slots for builtin operations */
    ucs_list_link_t          list;    /* member of a per-group list of plans */
    ucs_list_link_t          by_root; /* extra phases for non-zero root */
    ucs_mpool_t              op_mp;   /* memory pool for (builtin_)operations */
    ucg_step_idx_t           phs_cnt; /* number of phases in the normal flow */
    uint8_t                  ep_cnt;  /* total endpoint count */
    uint16_t                 am_id;   /* active message ID */
    ucg_builtin_plan_phase_t phss[];  /* topology's phases */
/*  uct_ep_h                 eps[];    * logically located here */
} ucg_builtin_plan_t;

ucs_status_t ucg_builtin_connect(ucg_builtin_group_ctx_t *ctx,
        ucg_group_member_index_t idx, ucg_builtin_plan_phase_t *phase,
        unsigned phase_ep_index, unsigned sm_coll_flags, int is_mock);

ucs_status_t ucg_builtin_single_connection_phase(ucg_builtin_group_ctx_t *ctx,
        ucg_group_member_index_t idx, ucg_step_idx_t step_index,
        enum ucg_builtin_plan_method_type method,
        enum ucg_plan_connect_flags flags,
        ucg_builtin_plan_phase_t *phase,
        int is_mock);

typedef struct ucg_builtin_config ucg_builtin_config_t;

typedef struct ucg_builtin_tree_config {
    unsigned radix;
#define UCG_BUILTIN_TREE_MAX_RADIX (32)
    unsigned sock_thresh;
} ucg_builtin_tree_config_t;

extern ucs_config_field_t ucg_builtin_tree_config_table[];

ucs_status_t ucg_builtin_bruck_create(ucg_builtin_group_ctx_t *ctx,
        const ucg_builtin_plan_topology_t *topology,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);

ucs_status_t ucg_builtin_pairwise_create(ucg_builtin_group_ctx_t *ctx,
        const ucg_builtin_plan_topology_t *topology,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);

ucs_status_t ucg_builtin_tree_create(ucg_builtin_group_ctx_t *ctx,
        const ucg_builtin_plan_topology_t *topology,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);

ucs_status_t ucg_builtin_topo_tree_set_root(ucg_group_member_index_t root,
        ucg_group_member_index_t my_index,
        ucg_builtin_plan_t *plan,
        ucg_builtin_plan_phase_t **first_phase_p,
        unsigned *phase_count_p);

typedef struct ucg_builtin_tree_params {
    const ucg_group_params_t           *group_params;
    const ucg_collective_type_t        *coll_type;
    const ucg_builtin_plan_topology_t  *topology;
    const ucg_builtin_tree_config_t    *config;
    ucg_group_member_index_t            root;
    ucg_builtin_group_ctx_t            *ctx;
} ucg_builtin_tree_params_t;

typedef struct ucg_builtin_topo_tree_root_phase {
    ucs_list_link_t          list;
    ucg_group_member_index_t root;
    ucg_step_idx_t           phs_cnt;
    ucg_builtin_plan_phase_t phss[UCG_BUILTIN_TREE_MAX_RADIX];
} ucg_builtin_topo_tree_root_phase_t;

ucs_status_t ucg_builtin_tree_connect(ucg_builtin_plan_t *tree,
        ucg_builtin_topo_tree_root_phase_t *root,
        const ucg_builtin_tree_params_t *params,
        ucg_step_idx_t step_offset, uct_ep_h *first_ep,
        ucg_group_member_index_t *host_up,   unsigned host_up_cnt,
        ucg_group_member_index_t *net_up,    unsigned net_up_cnt,
        ucg_group_member_index_t *net_down,  unsigned net_down_cnt,
        ucg_group_member_index_t *host_down, unsigned host_down_cnt);

ucs_status_t ucg_builtin_tree_add_intra(const ucg_builtin_tree_params_t *params,
        ucg_group_member_index_t *my_idx,
        unsigned *ppn,
        ucg_group_member_index_t *up,
        unsigned *final_up_cnt,
        ucg_group_member_index_t *down,
        unsigned *final_down_cnt,
        enum ucg_group_member_distance *master_phase);

typedef struct ucg_builtin_recursive_config {
    unsigned factor;
} ucg_builtin_recursive_config_t;

extern ucs_config_field_t ucg_builtin_recursive_config_table[];

ucs_status_t ucg_builtin_recursive_create(ucg_builtin_group_ctx_t *ctx,
        const ucg_builtin_plan_topology_t *topology,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);

typedef struct ucg_builtin_neighbor_config {
    unsigned dimension;
} ucg_builtin_neighbor_config_t;

extern ucs_config_field_t ucg_builtin_neighbor_config_table[];

ucs_status_t ucg_topo_neighbor_create(ucg_builtin_group_ctx_t *ctx,
        const ucg_builtin_plan_topology_t *topology,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p);

struct ucg_builtin_config {
    ucg_plan_config_t              super;

    ucg_builtin_tree_config_t      tree;
    ucg_builtin_recursive_config_t recursive;
    ucg_builtin_neighbor_config_t  neighbor;

    unsigned                       cache_size;
    size_t                         short_max_tx;
    size_t                         bcopy_max_tx;
    unsigned                       mem_reg_opt_cnt;
};

#endif
