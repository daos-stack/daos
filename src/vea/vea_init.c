/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
#include <daos/btree_class.h>
#include "vea_internal.h"

void
destroy_free_class(struct vea_free_class *vfc)
{
	/* Destroy the in-memory sized free extent tree */
	if (daos_handle_is_valid(vfc->vfc_size_btr)) {
		dbtree_destroy(vfc->vfc_size_btr, NULL);
		vfc->vfc_size_btr = DAOS_HDL_INVAL;
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

static struct d_binheap_ops heap_ops = {
	.hop_enter	= NULL,
	.hop_exit	= NULL,
	.hop_compare	= heap_node_cmp,
};

int
create_free_class(struct vea_free_class *vfc, struct vea_space_df *md)
{
	struct umem_attr	uma;
	int			rc;

	vfc->vfc_size_btr = DAOS_HDL_INVAL;
	rc = d_binheap_create_inplace(DBH_FT_NOLOCK, 0, NULL, &heap_ops,
				      &vfc->vfc_heap);
	if (rc != 0)
		return -DER_NOMEM;

	D_ASSERT(md->vsd_blk_sz > 0 && md->vsd_blk_sz <= (1U << 20));
	vfc->vfc_large_thresh = (VEA_LARGE_EXT_MB << 20) / md->vsd_blk_sz;

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	/* Create in-memory sized free extent tree */
	rc = dbtree_create(DBTREE_CLASS_IFV, BTR_FEAT_UINT_KEY, VEA_TREE_ODR, &uma, NULL,
			   &vfc->vfc_size_btr);
	if (rc != 0)
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
	struct umem_attr uma = {0};
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
