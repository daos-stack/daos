/**
 * (C) Copyright 2018 Intel Corporation.
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

#ifndef __VEA_INTERNAL_H__
#define __VEA_INTERNAL_H__

#include <gurt/list.h>
#include <gurt/heap.h>
#include <daos/mem.h>
#include <daos/btree.h>

#define VEA_MAGIC	(0xea201804)

/* Per I/O stream hint context */
struct vea_hint_context {
	struct vea_hint_df	*vhc_pd;
	/* In-memory hint block offset */
	uint64_t		 vhc_off;
	/* In-memory hint sequence */
	uint64_t		 vhc_seq;
};

/* Free extent informat stored in the in-memory compound free extent index */
struct vea_entry {
	struct vea_free_extent	ve_ext;
	/* Link to vfc_heap */
	struct d_binheap_node	ve_node;
	/* Link to one of vfc_lrus or vsi_agg_lru */
	d_list_t		ve_link;
	uint32_t		ve_in_heap:1;
};

#define VEA_LARGE_EXT_MB  64  /* Large extent threashold in MB */

/*
 * Large free extents (>=VEA_LARGE_EXT_MB) are tracked in max a heap, small
 * free extents (< VEA_LARGE_EXT_MB) are tracked in size categorized LRUs
 * respectively.
 */
struct vea_free_class {
	/* Max heap for tracking the largest free extent */
	struct d_binheap	 vfc_heap;
	/* Idle large free extent list */
	uint32_t		 vfc_large_thresh;
	/* How many size classed LRUs for small free extents */
	uint32_t		 vfc_lru_cnt;
	/* Extent size classed LRU lists  */
	d_list_t		*vfc_lrus;
	/*
	 * Upper size (in blocks) bounds for all size classes. The lower size
	 * bound of the size class is the upper bound of previous class (0 for
	 * first class), so the size of each extent in a size class satisfies:
	 * vfc_sizes[i + 1] < blk_cnt <= vfc_sizes[i].
	 */
	uint32_t		*vfc_sizes;
};

/* In-memory compound index */
struct vea_space_info {
	/* Instance for the pmemobj pool on SCM */
	struct umem_instance	*vsi_umem;
	/* Free space information stored on SCM */
	struct vea_space_df	*vsi_md;
	/* Open handles for the persistent free extent tree */
	daos_handle_t		 vsi_md_free_btr;
	/* Open handles for the persistent extent vector tree */
	daos_handle_t		 vsi_md_vec_btr;
	/* Free extent tree sorted by offset, for all free extents. */
	daos_handle_t		 vsi_free_btr;
	/* Extent vector tree, for non-contiguous allocation */
	daos_handle_t		 vsi_vec_btr;
	/* Reserved blocks in total */
	uint64_t		 vsi_tot_resrvd;
	/* Index for searching free extent by size & age */
	struct vea_free_class	 vsi_class;
	/* LRU to aggergate just recent freed extents */
	d_list_t		 vsi_agg_lru;
	/*
	 * Free extent tree sorted by offset, for coalescing the just recent
	 * free extents.
	 */
	daos_handle_t		 vsi_agg_btr;
	/* Last aggregation time */
	uint64_t		 vsi_agg_time;
	/* Unmap context to performe unmap against freed extent */
	struct vea_unmap_context	vsi_unmap_ctxt;
};

#endif /* __VEA_INTERNAL_H__ */
