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

#define DDSUBSYS	DDFAC(vos)

#include <daos_srv/vea.h>
#include "vea_internal.h"

/*
 * Initialize the space tracking information on SCM and the header of the
 * block device.
 */
int vea_format(struct umem_instance *umem, struct vea_space_df *md,
	       uint64_t dev_id, uint32_t blk_sz, uint32_t hdr_blks,
	       uint64_t capacity, vea_format_callback_t cb, void *cb_data,
	       bool force)
{
	return 0;
}

/*
 * Load space tracking information from SCM to initialize the in-memory
 * compound index.
 */
int vea_load(struct umem_instance *umem, struct vea_space_df *md,
	     struct vea_unmap_context *unmap_ctxt,
	     struct vea_space_info **vsip)
{
	return 0;
}

/* Free the memory footprint created by vea_load(). */
void vea_unload(struct vea_space_info *vsi)
{
}

/*
 * Reserve an extent on block device.
 *
 * Always try to preserve sequential locality by 'hint', 'free extent size'
 * and 'free extent age', if the block device is too fragmented to satisfy
 * a contiguous allocation, reserve an extent vector as the last resort.
 *
 * Reserve attempting order:
 *
 * 1. Reserve from the free extent with 'hinted' start offset. (vsi_free_tree)
 * 2. Reserve from the largest free extent if it isn't non-active (extent age
 *    isn't VEA_EXT_AGE_MAX), otherwise, if it's dividable (extent size > 2 *
 *    VEA_LARGE_EXT_MB), divide it in half-and-half and resreve from the latter
 *    half. (vfc_heap)
 * 3. Search & reserve from a bunch of extent size classed LRUs in first fit
 *    policy, larger & older free extent has priority. (vfc_lrus)
 * 4. Repeat the search in 3rd step to reserve an extent vector. (vsi_vec_tree)
 * 5. Fail reserve with ENOMEM if all above attempts fail.
 */
int vea_reserve(struct vea_space_info *vsi, uint32_t blk_cnt,
		struct vea_hint_context *hint,
		d_list_t *resrvd_list)
{
	return 0;
}

/* Cancel the reserved extent(s) */
int vea_cancel(struct vea_space_info *vsi, struct vea_hint_context *hint,
	       d_list_t *resrvd_list)
{
	return 0;
}

/*
 * Make the reservation persistent. It should be part of transaction
 * manipulated by caller.
 */
int vea_tx_publish(struct vea_space_info *vsi, struct vea_hint_context *hint,
		   d_list_t *resrvd_list)
{
	return 0;
}

/*
 * Free allocated extent. It should be part of transaction manipulated
 * by caller.
 *
 * The just recent freed extents won't be visible for allocation instantly,
 * they will stay in vsi_agg_lru for a short period time, and being coalesced
 * with each other there.
 *
 * Expired free extents in the vsi_agg_lru will be migrated to the allocation
 * visible index (vsi_free_tree, vfc_heap or vfc_lrus) from time to time, this
 * kind of migration will be triggered by vea_reserve() & vea_tx_free() calls.
 */
int vea_tx_free(struct vea_space_info *vsi, uint64_t blk_off, uint32_t blk_cnt)
{
	return 0;
}

/* Set an arbitrary age to a free extent with specified start offset. */
int vea_set_ext_age(struct vea_space_info *vsi, uint64_t blk_off, uint64_t age)
{
	return 0;
}

/* Convert an extent into an allocated extent vector. */
int vea_get_ext_vector(struct vea_space_info *vsi, uint64_t blk_off,
		       uint32_t blk_cnt, struct vea_ext_vector *ext_vector)
{
	return 0;
}

/* Load persistent hint data and initialize in-memory hint context */
int vea_hint_load(struct vea_hint_df *phd, struct vea_hint_context **thc)
{
	return 0;
}

/* Free memory foot-print created by vea_hint_load() */
void vea_hint_unload(struct vea_hint_context *thc)
{
}
