/**
 * (C) Copyright 2017-2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#ifndef __EVT_PRIV_H__
#define __EVT_PRIV_H__

#include <daos_srv/evtree.h>

/**
 * Tree node types.
 * NB: a node can be both root and leaf.
 */
enum {
	EVT_NODE_LEAF		= (1 << 0),	/**< leaf node */
	EVT_NODE_ROOT		= (1 << 1),	/**< root node */
};

enum evt_iter_state {
	EVT_ITER_NONE		= 0,	/**< uninitialized iterator */
	EVT_ITER_INIT,			/**< initialized but not probed */
	EVT_ITER_READY,			/**< probed, ready to iterate */
	EVT_ITER_FINI,			/**< reach at the end of iteration */
};

struct evt_iterator {
	/* Epoch range for the iterator */
	daos_epoch_range_t		it_epr;
	/** state of the iterator */
	unsigned short			it_state;
	/** iterator embedded in open handle */
	bool				it_private;
};

#define EVT_TRACE_MAX                   32

struct evt_trace {
	/** the current node mmid */
	TMMID(struct evt_node)		tr_node;
	/** child position of the searching trace */
	unsigned int			tr_at;
	/** Indicates whether node has been added to tx */
	bool				tr_tx_added;
};

struct evt_context {
	/** mapped address of the tree root */
	struct evt_root			*tc_root;
	/** memory ID of the tree root */
	TMMID(struct evt_root)		 tc_root_mmid;
	/** magic number to identify invalid tree open handle */
	unsigned int			 tc_magic;
	/** refcount on the context */
	unsigned int			 tc_ref;
	/** cached tree order (reduce PMEM access) */
	unsigned int			 tc_order;
	/** cached tree depth (reduce PMEM access) */
	unsigned int			 tc_depth;
	/** cached tree feature bits (reduce PMEM access) */
	uint64_t			 tc_feats;
	/** memory instance (PMEM or DRAM) */
	struct umem_instance		 tc_umm;
	/** pmemobj pool uuid */
	uint64_t			 tc_pmempool_uuid;
	/**
	 * NVMe free space tracking information used for NVMe record
	 * alloc & free
	 */
	void				*tc_blks_info;
	/** embedded iterator */
	struct evt_iterator		 tc_iter;
	/** space to store tree search path */
	struct evt_trace		 tc_traces[EVT_TRACE_MAX];
	/** trace root which points to &tc_traces[EVT_TRACE_MAX - depth] */
	struct evt_trace		*tc_trace;
	/** customized operation table for different tree policies */
	struct evt_policy_ops		*tc_ops;
};

#define EVT_NODE_NULL			TMMID_NULL(struct evt_node)
#define EVT_ROOT_NULL			TMMID_NULL(struct evt_root)

#define evt_umm(tcx)			(&(tcx)->tc_umm)
#define evt_mmid2ptr(tcx, mmid)		umem_id2ptr(evt_umm(tcx), mmid)
#define evt_tmmid2ptr(tcx, tmmid)	umem_id2ptr_typed(evt_umm(tcx), tmmid)
#define evt_has_tx(tcx)			umem_has_tx(evt_umm(tcx))

/* By definition, all rectangles overlap in the epoch range because all
 * are from start to infinity.  However, for common queries, we often only want
 * rectangles intersect at a given epoch
 */
enum evt_find_opc {
	/** find all rectangles overlapped with the input rectangle */
	EVT_FIND_ALL,
	/** find the first rectangle overlapped with the input rectangle */
	EVT_FIND_FIRST,
	/**
	 * Returns -DER_NO_PERM if any overlapping rectangle is found in
	 * the same epoch with an identical sequence number.
	 */
	EVT_FIND_OVERWRITE,
	/** Find the exactly same extent. */
	EVT_FIND_SAME,
};

int evt_tcx_clone(struct evt_context *tcx, struct evt_context **tcx_pp);

#define EVT_HDL_ALIVE	0xbabecafe
#define EVT_HDL_DEAD	0xdeadbeef

static inline void
evt_tcx_addref(struct evt_context *tcx)
{
	tcx->tc_ref++;
}

static inline void
evt_tcx_decref(struct evt_context *tcx)
{
	D_ASSERT(tcx->tc_ref > 0);
	tcx->tc_ref--;
	if (tcx->tc_ref == 0) {
		tcx->tc_magic = EVT_HDL_DEAD;
		D_FREE(tcx);
	}
}

daos_handle_t evt_tcx2hdl(struct evt_context *tcx);
struct evt_context *evt_hdl2tcx(daos_handle_t toh);
bool evt_move_trace(struct evt_context *tcx, bool forward,
		    daos_epoch_range_t *epr);

struct evt_rect *evt_node_rect_at(struct evt_context *tcx,
				  TMMID(struct evt_node) nd_mmid,
				  unsigned int at);

void evt_fill_entry(struct evt_context *tcx, TMMID(struct evt_node) nd_mmid,
		    unsigned int at, struct evt_rect *rect_srch,
		    struct evt_entry *entry);
int evt_find_ent_list(struct evt_context *tcx, enum evt_find_opc find_opc,
		      struct evt_rect *rect, struct evt_entry_list *ent_list);

#endif /* __EVT_PRIV_H__ */
