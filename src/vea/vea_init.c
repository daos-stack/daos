/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
#include "vea_internal.h"

void
destroy_free_class(struct vea_free_class *vfc)
{
	vfc->vfc_lru_cnt = 0;

	if (vfc->vfc_cursor) {
		D_FREE(vfc->vfc_cursor);
		vfc->vfc_cursor = NULL;
	}
	if (vfc->vfc_lrus) {
		D_FREE(vfc->vfc_lrus);
		vfc->vfc_lrus = NULL;
	}
	if (vfc->vfc_sizes) {
		D_FREE(vfc->vfc_sizes);
		vfc->vfc_sizes = NULL;
	}
	d_binheap_destroy_inplace(&vfc->vfc_heap);
}

static bool
heap_node_cmp(struct d_binheap_node *a, struct d_binheap_node *b)
{
	struct vea_entry *nodea, *nodeb;

	nodea = container_of(a, struct vea_entry, ve_node);
	nodeb = container_of(b, struct vea_entry, ve_node);

	/* Max heap, the largest free extent is heap root */
	return nodea->ve_ext.vfe_blk_cnt > nodeb->ve_ext.vfe_blk_cnt;
}

struct d_binheap_ops heap_ops = {
	.hop_enter	= NULL,
	.hop_exit	= NULL,
	.hop_compare	= heap_node_cmp,
};

int
create_free_class(struct vea_free_class *vfc, struct vea_space_df *md)
{
	uint32_t max_blks, min_blks;
	int rc, i, lru_cnt, size;

	rc = d_binheap_create_inplace(DBH_FT_NOLOCK, 0, NULL, &heap_ops,
				      &vfc->vfc_heap);
	if (rc != 0)
		return -DER_NOMEM;

	/*
	 * Divide free extents smaller than VEA_LARGE_EXT_MB into bunch of
	 * size classed groups, the size upper bound of each group will be
	 * max_blks, max_blks/2, max_blks/4 ... min_blks.
	 */
	D_ASSERT(md->vsd_blk_sz > 0 && md->vsd_blk_sz <= (1U << 20));
	max_blks = (VEA_LARGE_EXT_MB << 20) / md->vsd_blk_sz;
	min_blks = (1U << 20) / md->vsd_blk_sz;

	vfc->vfc_large_thresh = max_blks;
	lru_cnt = 1;
	while (max_blks > min_blks) {
		max_blks >>= 1;
		lru_cnt++;
	}

	D_ASSERT(vfc->vfc_lrus == NULL);
	D_ALLOC_ARRAY(vfc->vfc_lrus, lru_cnt);
	if (vfc->vfc_lrus == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	D_ASSERT(vfc->vfc_sizes == NULL);
	D_ALLOC_ARRAY(vfc->vfc_sizes, lru_cnt);
	if (vfc->vfc_sizes == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	max_blks = vfc->vfc_large_thresh;
	for (i = 0; i < lru_cnt; i++) {
		D_INIT_LIST_HEAD(&vfc->vfc_lrus[i]);
		vfc->vfc_sizes[i] = max_blks > min_blks ? max_blks : min_blks;
		max_blks >>= 1;
	}
	D_ASSERT(vfc->vfc_lru_cnt == 0);
	vfc->vfc_lru_cnt = lru_cnt;

	D_ASSERT(vfc->vfc_cursor == NULL);
	size = lru_cnt * sizeof(struct vea_entry *);
	D_ALLOC_ARRAY(vfc->vfc_cursor, size);
	if (vfc->vfc_cursor == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	return 0;
error:
	destroy_free_class(vfc);
	return rc;
}

void
unload_space_info(struct vea_space_info *vsi)
{
	if (daos_handle_is_valid(vsi->vsi_md_free_btr)) {
		dbtree_close(vsi->vsi_md_free_btr);
		vsi->vsi_md_free_btr = DAOS_HDL_INVAL;
	}

	if (daos_handle_is_valid(vsi->vsi_md_vec_btr)) {
		dbtree_close(vsi->vsi_md_vec_btr);
		vsi->vsi_md_vec_btr = DAOS_HDL_INVAL;
	}
}

static int
load_free_entry(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *arg)
{
	struct vea_free_extent *vfe;
	struct vea_space_info *vsi;
	uint64_t *off;
	int rc;

	vsi = (struct vea_space_info *)arg;
	off = (uint64_t *)key->iov_buf;
	vfe = (struct vea_free_extent *)val->iov_buf;

	rc = verify_free_entry(off, vfe);
	if (rc != 0)
		return rc;

	rc = compound_free(vsi, vfe, VEA_FL_NO_MERGE);
	if (rc != 0)
		return rc;

	return 0;
}

static int
load_vec_entry(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *arg)
{
	struct vea_ext_vector *vec;
	struct vea_space_info *vsi;
	uint64_t *off;
	int rc;

	vsi = (struct vea_space_info *)arg;
	off = (uint64_t *)key->iov_buf;
	vec = (struct vea_ext_vector *)val->iov_buf;

	rc = verify_vec_entry(off, vec);
	if (rc != 0)
		return rc;

	return compound_vec_alloc(vsi, vec);
}

int
load_space_info(struct vea_space_info *vsi)
{
	struct umem_attr uma;
	int rc;

	D_ASSERT(vsi->vsi_umem != NULL);
	D_ASSERT(vsi->vsi_md != NULL);

	/* Open SCM free extent tree */
	uma.uma_id = vsi->vsi_umem->umm_id;
	uma.uma_pool = vsi->vsi_umem->umm_pool;

	D_ASSERT(daos_handle_is_inval(vsi->vsi_md_free_btr));
	rc = dbtree_open_inplace(&vsi->vsi_md->vsd_free_tree, &uma,
				 &vsi->vsi_md_free_btr);
	if (rc != 0)
		goto error;

	/* Open SCM extent vector tree */
	D_ASSERT(daos_handle_is_inval(vsi->vsi_md_vec_btr));
	rc = dbtree_open_inplace(&vsi->vsi_md->vsd_vec_tree, &uma,
				 &vsi->vsi_md_vec_btr);
	if (rc != 0)
		goto error;

	/* Build up in-memory compound free extent index */
	rc = dbtree_iterate(vsi->vsi_md_free_btr, DAOS_INTENT_DEFAULT, false,
			    load_free_entry, (void *)vsi);
	if (rc != 0)
		goto error;

	/* Build up in-memory extent vector tree */
	rc = dbtree_iterate(vsi->vsi_md_vec_btr, DAOS_INTENT_DEFAULT, false,
			    load_vec_entry, (void *)vsi);
	if (rc != 0)
		goto error;

	return 0;
error:
	unload_space_info(vsi);
	return rc;
}
