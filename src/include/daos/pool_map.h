/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/include/daos/pool_map.h
 */

#ifndef __DAOS_POOL_MAP_H__
#define __DAOS_POOL_MAP_H__

#include <daos/common.h>

#define POOL_MAP_VER_1		(1)
#define POOL_MAP_VER_2		(2)
#define POOL_MAP_VERSION	POOL_MAP_VER_2

#define DF_TARGET "Target[%d] (rank %u idx %u status %u)"
#define DP_TARGET(t) t->ta_comp.co_id, t->ta_comp.co_rank, t->ta_comp.co_index,\
		     t->ta_comp.co_status

/**
 * pool component types
 * target and node are pre-registered and managed by DAOS.
 * Level 2 to 254 are allocated by control plane based on input from
 * yaml file.
 */
typedef enum pool_comp_type {
	PO_COMP_TP_TARGET	= 0, /** reserved, hard-coded */
	PO_COMP_TP_RANK		= 1, /** reserved, hard-coded */
	PO_COMP_TP_MIN		= 2, /** first user-defined domain */
	PO_COMP_TP_NODE		= 2, /** for test only */
	PO_COMP_TP_MAX		= 254, /** last user-defined domain */
	PO_COMP_TP_ROOT		= 255,
	PO_COMP_TP_END		= 256,
} pool_comp_type_t;

/** pool component states */
typedef enum pool_comp_state {
	PO_COMP_ST_UNKNOWN	= 0,
	/** intermediate state for pool map change */
	PO_COMP_ST_NEW		= 1,
	/** component is healthy */
	PO_COMP_ST_UP		= 1 << 1,
	/** component is healthy and integrated in storage pool */
	PO_COMP_ST_UPIN		= 1 << 2,
	/** component is dead */
	PO_COMP_ST_DOWN		= 1 << 3,
	/** component is dead, its data has already been rebuilt */
	PO_COMP_ST_DOWNOUT	= 1 << 4,
	/** component is currently being drained and rebuilt elsewhere */
	PO_COMP_ST_DRAIN	= 1 << 5,
} pool_comp_state_t;

enum pool_component_flags {
	PO_COMPF_NONE		= 0,
	/**
	 * indicate when in status PO_COMP_ST_DOWNOUT, it is changed from
	 * PO_COMP_ST_DOWN (rather than from PO_COMP_ST_DRAIN).
	 */
	PO_COMPF_DOWN2OUT	= 1,
};

#define co_in_ver	co_out_ver
/** parent class of all all pool components: target, domain */
struct pool_component {
	/** pool_comp_type_t */
	uint8_t		co_type;
	/** pool_comp_state_t */
	uint8_t		co_status;
	/** target index inside the node */
	uint8_t		co_index;
	/** padding for 64-bit alignment */
	uint8_t		co_padding;
	/** Immutable component ID. */
	uint32_t	co_id;
	/**
	 * e.g. rank in the communication group, only used by PO_COMP_TARGET
	 * for the time being.
	 */
	uint32_t	co_rank;
	/** version it's been added */
	uint32_t	co_ver;
	/** failure sequence */
	uint32_t		co_fseq;

	/**
	 * co_in_ver and co_out_ver are shared the same item here.
	 * If the target status(co_status) is PO_COMP_ST_DOWNOUT.
	 * it means the map version when the target is excluded.
	 * Otherwise, it is the map version when the target is
	 * extended or reintegrated.
	 */
	uint32_t		co_out_ver; /* co_in_ver */

	/** flags, see enum pool_component_flags */
	uint32_t		co_flags;
	/** number of children or storage partitions */
	uint32_t	co_nr;
};

/** a leaf of pool map */
struct pool_target {
	/** embedded component for myself */
	struct pool_component	 ta_comp;
	/** nothing else for the time being */
};

/**
 * an intermediate component in pool map, a domain can either contains low
 * level domains or just leaf targets.
 */
struct pool_domain {
	/** embedded component for myself */
	struct pool_component	 do_comp;
	/** # all targets within this domain */
	unsigned int		 do_target_nr;
	/**
	 * child domains within current domain, it is NULL for the last
	 * level domain.
	 */
	struct pool_domain	*do_children;
	/**
	 * all targets within this domain
	 * for the last level domain, it points to the first direct targets
	 * for the intermediate domain, it points to the first indirect targets
	 */
	struct pool_target	*do_targets;
};

#define do_child_nr		do_comp.co_nr
#define do_cpu_nr		do_comp.co_nr

/** Target address for each pool target */
struct pool_target_addr {
	/** rank of the node where the target resides */
	d_rank_t	pta_rank;
	/** target index within each node */
	uint32_t	pta_target;
};

struct pool_target_addr_list {
	int			 pta_number;
	struct pool_target_addr	*pta_addrs;
};

int
pool_target_addr_list_alloc(unsigned int num,
			    struct pool_target_addr_list *list);
int
pool_target_addr_list_append(struct pool_target_addr_list *dst_list,
			     struct pool_target_addr *src);
void
pool_target_addr_list_free(struct pool_target_addr_list *list);

struct pool_target_id {
	uint32_t	pti_id;
};

struct pool_target_id_list {
	int			pti_number;
	struct pool_target_id	*pti_ids;
};

int
pool_target_id_list_append(struct pool_target_id_list *id_list,
			   struct pool_target_id *id);
int
pool_target_id_list_merge(struct pool_target_id_list *dst_list,
			  struct pool_target_id_list *src_list);

int
pool_target_id_list_alloc(unsigned int num,
			  struct pool_target_id_list *id_list);

void
pool_target_id_list_free(struct pool_target_id_list *id_list);

/**
 * pool component buffer, it's a contiguous buffer which includes portion of
 * or all components of a pool map.
 */
struct pool_buf {
	/** format version */
	uint32_t		pb_version;
	/** reserved, for alignment now */
	uint32_t		pb_reserved;
	/** checksum of components */
	uint32_t	pb_csum;
	/** summary of domain_nr, node_nr, target_nr, buffer size */
	uint32_t	pb_nr;
	uint32_t	pb_domain_nr;
	uint32_t	pb_node_nr;
	uint32_t	pb_target_nr;
	uint32_t	pb_padding;

	/** buffer body */
	struct pool_component	pb_comps[0];
};

static inline long pool_buf_size(unsigned int nr)
{
	return offsetof(struct pool_buf, pb_comps[nr]);
}

static inline unsigned int pool_buf_nr(size_t size)
{
	return (size - offsetof(struct pool_buf, pb_comps[0])) /
		sizeof(struct pool_component);
}

struct pool_map;

struct pool_buf *pool_buf_alloc(unsigned int nr);
struct pool_buf *pool_buf_dup(struct pool_buf *buf);
void pool_buf_free(struct pool_buf *buf);
int  pool_buf_extract(struct pool_map *map, struct pool_buf **buf_pp);
int  pool_buf_attach(struct pool_buf *buf, struct pool_component *comps,
		     unsigned int comp_nr);
int gen_pool_buf(struct pool_map *map, struct pool_buf **map_buf_out, int map_version, int ndomains,
		 int nnodes, int ntargets, const uint32_t *domains, const d_rank_list_t *ranks,
		 uint32_t dss_tgt_nr);

int pool_map_comp_cnt(struct pool_map *map);

int  pool_map_create(struct pool_buf *buf, uint32_t version,
		     struct pool_map **mapp);
void pool_map_addref(struct pool_map *map);
void pool_map_decref(struct pool_map *map);
int  pool_map_extend(struct pool_map *map, uint32_t version,
		     struct pool_buf *buf);
void pool_map_print(struct pool_map *map);

int  pool_map_set_version(struct pool_map *map, uint32_t version);
uint32_t pool_map_get_version(struct pool_map *map);

int pool_map_get_failed_cnt(struct pool_map *map, uint32_t domain);

#define PO_COMP_ID_ALL		(-1)

int pool_map_find_target(struct pool_map *map, uint32_t id,
			 struct pool_target **target_pp);
int pool_map_find_domain(struct pool_map *map, pool_comp_type_t type,
			 uint32_t id, struct pool_domain **domain_pp);
int pool_map_find_nodes(struct pool_map *map, uint32_t id,
			struct pool_domain **domain_pp);
int pool_map_find_tgts_by_state(struct pool_map *map,
				pool_comp_state_t match_states,
				struct pool_target **tgt_pp,
				unsigned int *tgt_cnt);
int pool_map_find_up_tgts(struct pool_map *map, struct pool_target **tgt_pp,
			  unsigned int *tgt_cnt);
int pool_map_find_down_tgts(struct pool_map *map, struct pool_target **tgt_pp,
			    unsigned int *tgt_cnt);
int pool_map_find_failed_tgts(struct pool_map *map, struct pool_target **tgt_pp,
			      unsigned int *tgt_cnt);
int pool_map_find_upin_tgts(struct pool_map *map, struct pool_target **tgt_pp,
			  unsigned int *tgt_cnt);
int pool_map_update_failed_cnt(struct pool_map *map);
int pool_map_find_targets_on_ranks(struct pool_map *map,
				   d_rank_list_t *rank_list,
				   struct pool_target_id_list *tgts);
int pool_map_find_target_by_rank_idx(struct pool_map *map, uint32_t rank,
				 uint32_t tgt_idx, struct pool_target **tgts);
int pool_map_find_failed_tgts_by_rank(struct pool_map *map,
				  struct pool_target ***tgt_ppp,
				  unsigned int *tgt_cnt, d_rank_t rank);
int pool_map_activate_new_target(struct pool_map *map, uint32_t id);
bool
pool_map_node_status_match(struct pool_domain *dom, unsigned int status);

struct pool_domain *
pool_map_find_node_by_rank(struct pool_map *map, uint32_t rank);

int pool_map_find_by_rank_status(struct pool_map *map,
				 struct pool_target ***tgt_ppp,
				 unsigned int *tgt_cnt, unsigned int status,
				 d_rank_t rank);

static inline struct pool_target *
pool_map_targets(struct pool_map *map)
{
	struct pool_target *targets;
	int		    rc;

	rc = pool_map_find_target(map, PO_COMP_ID_ALL, &targets);
	D_ASSERT(rc >= 0);
	return rc == 0 ? NULL : targets;
}

static inline unsigned int
pool_map_target_nr(struct pool_map *map)
{
	return pool_map_find_target(map, PO_COMP_ID_ALL, NULL);
}

static inline unsigned int
pool_map_node_nr(struct pool_map *map)
{
	return pool_map_find_nodes(map, PO_COMP_ID_ALL, NULL);
}

/*
 *  Returns true if the target is not available for use.
 *  When a target is in the UP state it is considered unavailable
 *  until it is fully reintegrated or added to the pool except as part of
 *  the reintegration/addition calls to placement.
 *
 * param[in]	tgt		The pool target who's availability is being
 *				checked.
 * param[in]	for_reint	True if this target is being checked as part
 *				of the reintegration API call.
 *
 * return	True if the target is not available, otherwise false.
 *
 */
static inline bool
pool_component_unavail(struct pool_component *comp, bool for_reint)
{
	uint8_t status = comp->co_status;

	/* If it's down or down-out it is definitely unavailable */
	if ((status == PO_COMP_ST_DOWN) || (status == PO_COMP_ST_DOWNOUT))
		return true;

	/* Targets being drained should not be used */
	if (status == PO_COMP_ST_DRAIN)
		return true;

	/*
	 * The component is unavailable if it's currently being reintegrated.
	 * However when calculating the data movement for reintegration
	 * We treat these nodes as being available for the placement map.
	 */
	if ((status == PO_COMP_ST_UP) && (for_reint == false))
		return true;

	return false;
}

static inline bool
pool_target_unavail(struct pool_target *tgt, bool for_reint)
{
	return pool_component_unavail(&tgt->ta_comp, for_reint);
}

static inline bool
pool_target_avail(struct pool_target *tgt, uint32_t allow_status)
{
	return tgt->ta_comp.co_status & allow_status;
}

/** Check if the target is in PO_COMP_ST_DOWN status */
static inline bool
pool_target_down(struct pool_target *tgt)
{
	struct pool_component	*comp = &tgt->ta_comp;
	uint8_t			 status = comp->co_status;

	return (status == PO_COMP_ST_DOWN);
}

int pool_map_rf_verify(struct pool_map *map, uint32_t last_ver, uint32_t rf);
pool_comp_state_t pool_comp_str2state(const char *name);
const char *pool_comp_state2str(pool_comp_state_t state);

pool_comp_type_t pool_comp_abbr2type(char abbr);
pool_comp_type_t pool_comp_str2type(const char *name);
const char *pool_comp_type2str(pool_comp_type_t type);

static inline const char *
pool_comp_name(struct pool_component *comp)
{
	return pool_comp_type2str(comp->co_type);
}

#define pool_target_name(target)	pool_comp_name(&(target)->ta_comp)
#define pool_domain_name(domain)	pool_comp_name(&(domain)->do_comp)

#endif /* __DAOS_POOL_MAP_H__ */
