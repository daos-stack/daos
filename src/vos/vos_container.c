/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
/**
 * VOS Container API implementation
 * vos/vos_container.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos_srv/vos.h>
#include <daos_errno.h>
#include <daos/common.h>
#include <daos/mem.h>
#include <gurt/hash.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <vos_internal.h>
#include <vos_obj.h>

/**
 * Parameters for vos_cont_df btree
 */
struct cont_df_args {
	struct vos_cont_df	*ca_cont_df;
	struct vos_pool		*ca_pool;
};

static int
cont_df_hkey_size(void)
{
	return sizeof(struct d_uuid);
}

static int
cont_df_rec_msize(int alloc_overhead)
{
	return alloc_overhead + sizeof(struct vos_cont_df);
}


static void
cont_df_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct d_uuid));
	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
cont_df_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct umem_instance	*umm = &tins->ti_umm;

	if (UMOFF_IS_NULL(rec->rec_off))
		return -DER_NONEXIST;

	umem_free(umm, rec->rec_off);
	return 0;
}

static int
cont_df_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	     d_iov_t *val_iov, struct btr_record *rec)
{
	umem_off_t			 offset;
	struct vos_cont_df		*cont_df;
	struct cont_df_args		*args = NULL;
	struct d_uuid			*ukey = NULL;
	int				rc = 0;

	D_ASSERT(key_iov->iov_len == sizeof(struct d_uuid));
	ukey = (struct d_uuid *)key_iov->iov_buf;
	D_DEBUG(DB_DF, "Allocating container uuid=%s\n", DP_UUID(ukey->uuid));

	args = (struct cont_df_args *)(val_iov->iov_buf);
	offset = umem_zalloc(&tins->ti_umm, sizeof(struct vos_cont_df));
	if (UMOFF_IS_NULL(offset))
		return -DER_NOMEM;

	cont_df = umem_off2ptr(&tins->ti_umm, offset);
	uuid_copy(cont_df->cd_id, ukey->uuid);
	args->ca_cont_df = cont_df;
	rec->rec_off = offset;

	rc = vos_obj_tab_create(args->ca_pool, &cont_df->cd_otab_df);
	if (rc) {
		D_ERROR("VOS object index create failure\n");
		D_GOTO(exit, rc);
	}

	rc = vos_dtx_table_create(args->ca_pool, &cont_df->cd_dtx_table_df);
	if (rc) {
		D_ERROR("Failed to create DTX table: rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	return 0;

exit:
	vos_dtx_table_destroy(args->ca_pool, &cont_df->cd_dtx_table_df);
	if (cont_df->cd_otab_df.obt_btr.tr_class != 0)
		vos_obj_tab_destroy(args->ca_pool, &cont_df->cd_otab_df);
	cont_df_rec_free(tins, rec, NULL);

	return rc;
}

static int
cont_df_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct vos_cont_df		*cont_df;
	struct cont_df_args		*args = NULL;

	cont_df = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	args = (struct cont_df_args *)val_iov->iov_buf;
	args->ca_cont_df = cont_df;
	val_iov->iov_len = sizeof(struct cont_df_args);

	return 0;
}

static int
cont_df_rec_update(struct btr_instance *tins, struct btr_record *rec,
		   d_iov_t *key, d_iov_t *val)
{
	D_DEBUG(DB_DF, "Record exists already. Nothing to do\n");
	return 0;
}

static btr_ops_t vct_ops = {
	.to_rec_msize	= cont_df_rec_msize,
	.to_hkey_size	= cont_df_hkey_size,
	.to_hkey_gen	= cont_df_hkey_gen,
	.to_rec_alloc	= cont_df_rec_alloc,
	.to_rec_free	= cont_df_rec_free,
	.to_rec_fetch	= cont_df_rec_fetch,
	.to_rec_update  = cont_df_rec_update,
};

static int
cont_df_lookup(struct vos_pool *vpool, struct d_uuid *ukey,
	       struct cont_df_args *args)
{
	d_iov_t	key, value;

	d_iov_set(&key, ukey, sizeof(struct d_uuid));
	d_iov_set(&value, args, sizeof(struct cont_df_args));
	return dbtree_lookup(vpool->vp_cont_th, &key, &value);
}

/**
 * Container cache secondary key
 * comparison
 */
bool
cont_cmp(struct d_ulink *ulink, void *cmp_args)
{
	struct vos_container *cont;
	struct d_uuid	     *pkey;

	pkey = (struct d_uuid *)cmp_args;
	cont = container_of(ulink, struct vos_container, vc_uhlink);
	return !uuid_compare(cont->vc_pool->vp_id, pkey->uuid);
}

/**
 * Container cache functions
 */
void
cont_free(struct d_ulink *ulink)
{
	struct vos_container	*cont;
	int			 i;

	cont = container_of(ulink, struct vos_container, vc_uhlink);
	if (!daos_handle_is_inval(cont->vc_dtx_cos_hdl))
		dbtree_destroy(cont->vc_dtx_cos_hdl);
	D_ASSERT(d_list_empty(&cont->vc_dtx_committable));
	dbtree_close(cont->vc_dtx_active_hdl);
	dbtree_close(cont->vc_dtx_committed_hdl);
	dbtree_close(cont->vc_btr_hdl);

	for (i = 0; i < VOS_IOS_CNT; i++) {
		if (cont->vc_hint_ctxt[i])
			vea_hint_unload(cont->vc_hint_ctxt[i]);
	}

	D_FREE(cont);
}

struct d_ulink_ops   co_hdl_uh_ops = {
	.uop_free       = cont_free,
	.uop_cmp	= cont_cmp,
};

int
cont_insert(struct vos_container *cont, struct d_uuid *key, struct d_uuid *pkey,
	    daos_handle_t *coh)
{
	int	rc	= 0;

	D_ASSERT(cont != NULL && coh != NULL);

	d_uhash_ulink_init(&cont->vc_uhlink, &co_hdl_uh_ops);
	rc = d_uhash_link_insert(vos_cont_hhash_get(), key,
				 pkey, &cont->vc_uhlink);
	if (rc) {
		D_ERROR("UHASH table container handle insert failed\n");
		D_GOTO(exit, rc);
	}

	*coh = vos_cont2hdl(cont);
exit:
	return rc;
}



static int
cont_lookup(struct d_uuid *key, struct d_uuid *pkey,
	    struct vos_container **cont) {

	struct d_ulink *ulink;

	ulink = d_uhash_link_lookup(vos_cont_hhash_get(), key, pkey);
	if (ulink == NULL)
		return -DER_NONEXIST;

	*cont = container_of(ulink, struct vos_container, vc_uhlink);
	return 0;
}

static void
cont_decref(struct vos_container *cont)
{
	d_uhash_link_putref(vos_cont_hhash_get(), &cont->vc_uhlink);
}

static void
cont_addref(struct vos_container *cont)
{
	d_uhash_link_addref(vos_cont_hhash_get(), &cont->vc_uhlink);
}

static int
cont_close(struct vos_container *cont)
{
	d_uhash_link_delete(vos_cont_hhash_get(), &cont->vc_uhlink);
	return 0;
}

/**
 * Create a container within a VOS pool
 */
int
vos_cont_create(daos_handle_t poh, uuid_t co_uuid)
{

	struct vos_pool		*vpool = NULL;
	struct cont_df_args	 args;
	struct d_uuid		 ukey;
	d_iov_t		 key, value;
	int			 rc = 0;

	vpool = vos_hdl2pool(poh);
	if (vpool == NULL) {
		D_ERROR("Empty pool handle?\n");
		return -DER_INVAL;
	}

	D_DEBUG(DB_TRACE, "looking up co_id in container index\n");
	uuid_copy(ukey.uuid, co_uuid);
	args.ca_pool = vpool;

	rc = cont_df_lookup(vpool, &ukey, &args);
	if (!rc) {
		/* Check if attemt to reuse the same container uuid */
		D_ERROR("Container already exists\n");
		D_GOTO(exit, rc = -DER_EXIST);
	}

	rc = vos_tx_begin(vpool);
	if (rc != 0)
		goto exit;

	d_iov_set(&key, &ukey, sizeof(ukey));
	d_iov_set(&value, &args, sizeof(args));

	rc = dbtree_update(vpool->vp_cont_th, &key, &value);

	rc = vos_tx_end(vpool, rc);
exit:
	return rc;
}

/**
 * Open a container within a VOSP
 */
int
vos_cont_open(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh)
{

	int				rc = 0;
	struct vos_pool			*vpool = NULL;
	struct d_uuid			ukey;
	struct d_uuid			pkey;
	struct cont_df_args		args;
	struct vos_container		*cont = NULL;
	struct umem_attr		uma;

	D_DEBUG(DB_TRACE, "Open container "DF_UUID"\n", DP_UUID(co_uuid));

	vpool = vos_hdl2pool(poh);
	if (vpool == NULL) {
		D_ERROR("Empty pool handle?\n");
		return -DER_INVAL;
	}
	uuid_copy(pkey.uuid, vpool->vp_id);
	uuid_copy(ukey.uuid, co_uuid);

	/**
	 * Check if handle exists
	 * then return the handle immediately
	 */
	rc = cont_lookup(&ukey, &pkey, &cont);
	if (rc == 0) {
		D_DEBUG(DB_TRACE, "Found handle in DRAM UUID hash\n");
		*coh = vos_cont2hdl(cont);
		D_GOTO(exit, rc);
	}

	rc = cont_df_lookup(vpool, &ukey, &args);
	if (rc) {
		D_DEBUG(DB_TRACE, DF_UUID" container does not exist\n",
			DP_UUID(co_uuid));
		D_GOTO(exit, rc);
	}

	D_ALLOC_PTR(cont);
	if (!cont) {
		D_ERROR("Error in allocating container handle\n");
		D_GOTO(exit, rc = -DER_NOMEM);
	}

	uuid_copy(cont->vc_id, co_uuid);
	cont->vc_pool	 = vpool;
	cont->vc_cont_df = args.ca_cont_df;
	cont->vc_otab_df = &args.ca_cont_df->cd_otab_df;
	cont->vc_dtx_cos_hdl = DAOS_HDL_INVAL;
	D_INIT_LIST_HEAD(&cont->vc_dtx_committable);
	cont->vc_dtx_committable_count = 0;

	/* Cache this btr object ID in container handle */
	rc = dbtree_open_inplace_ex(&cont->vc_otab_df->obt_btr,
				    &cont->vc_pool->vp_uma,
				    vos_cont2hdl(cont),
				    cont->vc_pool->vp_vea_info,
				    &cont->vc_btr_hdl);
	if (rc) {
		D_ERROR("No Object handle, Tree open failed\n");
		D_GOTO(exit, rc);
	}

	rc = dbtree_open_inplace(
			&cont->vc_cont_df->cd_dtx_table_df.tt_committed_btr,
			&vpool->vp_uma, &cont->vc_dtx_committed_hdl);
	if (rc) {
		D_ERROR("Failed to open committed DTX table: rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	rc = dbtree_open_inplace(
			&cont->vc_cont_df->cd_dtx_table_df.tt_active_btr,
			&vpool->vp_uma, &cont->vc_dtx_active_hdl);
	if (rc) {
		D_ERROR("Failed to open active DTX table: rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	memset(&cont->vc_dtx_cos_btr, 0, sizeof(cont->vc_dtx_cos_btr));
	rc = dbtree_create_inplace(VOS_BTR_DTX_COS, 0, VOS_CONT_ORDER, &uma,
				   &cont->vc_dtx_cos_btr,
				   &cont->vc_dtx_cos_hdl);
	if (rc != 0) {
		D_ERROR("Failed to create DTX CoS btree: rc = %d\n", rc);
		D_GOTO(exit, rc);
	}

	if (cont->vc_pool->vp_vea_info != NULL) {
		int	i;

		for (i = 0; i < VOS_IOS_CNT; i++) {
			rc = vea_hint_load(&cont->vc_cont_df->cd_hint_df[i],
					   &cont->vc_hint_ctxt[i]);
			if (rc) {
				D_ERROR("Error loading allocator %d hint "
					DF_UUID": %d\n", i, DP_UUID(co_uuid),
					rc);
				goto exit;
			}
		}
	}

	rc = cont_insert(cont, &ukey, &pkey, coh);
	if (rc) {
		D_ERROR("Error inserting vos container handle to uuid hash\n");
		D_GOTO(exit, rc);
	}

exit:
	if (rc != 0 && cont)
		cont_decref(cont);

	return rc;
}

/**
 * Release container open handle
 */
int
vos_cont_close(daos_handle_t coh)
{
	struct vos_container	*cont;

	cont = vos_hdl2cont(coh);
	if (cont == NULL) {
		D_ERROR("Cannot close a NULL handle\n");
		return -DER_NO_HDL;
	}

	vos_obj_cache_evict(vos_obj_cache_current(), cont);
	cont_close(cont);
	cont_decref(cont);

	return 0;
}

/**
 * Query container information
 */
int
vos_cont_query(daos_handle_t coh, vos_cont_info_t *cont_info)
{
	struct vos_container	*cont;

	cont = vos_hdl2cont(coh);
	if (cont == NULL) {
		D_ERROR("Empty container handle for querying?\n");
		return -DER_INVAL;
	}

	cont_info->ci_nobjs = cont->vc_cont_df->cd_nobjs;
	cont_info->ci_used = cont->vc_cont_df->cd_used;
	cont_info->ci_hae = cont->vc_cont_df->cd_hae;

	return 0;
}

/**
 * Destroy a container
 */
int
vos_cont_destroy(daos_handle_t poh, uuid_t co_uuid)
{

	struct vos_pool			*vpool;
	struct vos_container		*cont = NULL;
	struct cont_df_args		 args;
	struct d_uuid			 pkey;
	struct d_uuid			 key;
	d_iov_t			 iov;
	int				 rc;

	uuid_copy(key.uuid, co_uuid);
	D_DEBUG(DB_TRACE, "Destroying CO ID in container index "DF_UUID"\n",
		DP_UUID(key.uuid));

	vpool = vos_hdl2pool(poh);
	if (vpool == NULL) {
		D_ERROR("Empty pool handle for destroying container?\n");
		return -DER_INVAL;
	}
	uuid_copy(pkey.uuid, vpool->vp_id);

	rc = cont_lookup(&key, &pkey, &cont);
	if (rc != -DER_NONEXIST) {
		D_ERROR("Open reference exists, cannot destroy\n");
		cont_decref(cont);
		D_GOTO(exit, rc = -DER_BUSY);
	}

	rc = cont_df_lookup(vpool, &key, &args);
	if (rc) {
		D_DEBUG(DB_TRACE, DF_UUID" container does not exist\n",
			DP_UUID(co_uuid));
		D_GOTO(exit, rc);
	}

	rc = vos_tx_begin(vpool);
	if (rc != 0)
		goto failed;

	rc = vos_obj_tab_destroy(vpool, &args.ca_cont_df->cd_otab_df);
	if (rc) {
		D_ERROR("OI destroy failed with error : %d\n", rc);
		goto end;
	}

	d_iov_set(&iov, &key, sizeof(struct d_uuid));
	rc = dbtree_delete(vpool->vp_cont_th, &iov, NULL);

end:
	rc = vos_tx_end(vpool, rc);
failed:
	if (rc != 0)
		D_ERROR("Destroying container transaction failed %d\n", rc);
exit:
	return rc;
}

void
vos_cont_addref(struct vos_container *cont)
{
	cont_addref(cont);
}

void
vos_cont_decref(struct vos_container *cont)
{
	cont_decref(cont);
}

/**
 * Internal Usage API
 * For use from container APIs and int APIs
 */

int
vos_cont_tab_register()
{
	int	rc;

	D_DEBUG(DB_DF, "Registering Container table class: %d\n",
		VOS_BTR_CONT_TABLE);

	rc = dbtree_class_register(VOS_BTR_CONT_TABLE, 0, &vct_ops);
	if (rc)
		D_ERROR("dbtree create failed\n");
	return rc;
}

int
vos_cont_tab_create(struct umem_attr *p_umem_attr,
		    struct vos_cont_table_df *ctab_df)
{

	int			rc = 0;
	daos_handle_t		btr_hdl;

	D_ASSERT(ctab_df->ctb_btree.tr_class == 0);
	D_DEBUG(DB_DF, "Create container table, type=%d\n", VOS_BTR_CONT_TABLE);

	rc = dbtree_create_inplace(VOS_BTR_CONT_TABLE, 0, VOS_CONT_ORDER,
				   p_umem_attr, &ctab_df->ctb_btree, &btr_hdl);
	if (rc) {
		D_ERROR("DBtree create failed\n");
		D_GOTO(exit, rc);
	}

	rc = dbtree_close(btr_hdl);
	if (rc)
		D_ERROR("Error in closing btree handle\n");

exit:
	return rc;
}

/** iterator for co_uuid */
struct cont_iterator {
	struct vos_iterator		 cot_iter;
	/* Handle of iterator */
	daos_handle_t			 cot_hdl;
	/* Pool handle */
	struct vos_pool			*cot_pool;
};

static struct cont_iterator *
vos_iter2co_iter(struct vos_iterator *iter)
{
	return container_of(iter, struct cont_iterator, cot_iter);
}

static int
cont_iter_fini(struct vos_iterator *iter)
{
	int			rc = 0;
	struct cont_iterator	*co_iter;

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);

	co_iter = vos_iter2co_iter(iter);

	if (!daos_handle_is_inval(co_iter->cot_hdl)) {
		rc = dbtree_iter_finish(co_iter->cot_hdl);
		if (rc)
			D_ERROR("co_iter_fini failed: %d\n", rc);
	}

	if (co_iter->cot_pool != NULL)
		vos_pool_decref(co_iter->cot_pool);

	D_FREE(co_iter);
	return rc;
}

int
cont_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	       struct vos_iterator **iter_pp)
{
	struct cont_iterator	*co_iter = NULL;
	struct vos_pool		*vpool = NULL;
	int			rc = 0;

	if (type != VOS_ITER_COUUID) {
		D_ERROR("Expected Type: %d, got %d\n",
			VOS_ITER_COUUID, type);
		return -DER_INVAL;
	}

	vpool = vos_hdl2pool(param->ip_hdl);
	if (vpool == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(co_iter);
	if (co_iter == NULL)
		return -DER_NOMEM;

	vos_pool_addref(vpool);
	co_iter->cot_pool = vpool;
	co_iter->cot_iter.it_type = type;

	rc = dbtree_iter_prepare(vpool->vp_cont_th, 0, &co_iter->cot_hdl);
	if (rc)
		D_GOTO(exit, rc);

	*iter_pp = &co_iter->cot_iter;
	return 0;
exit:
	cont_iter_fini(&co_iter->cot_iter);
	return rc;
}

static int
cont_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
		  daos_anchor_t *anchor)
{
	struct cont_iterator	*co_iter = vos_iter2co_iter(iter);
	d_iov_t		key, value;
	struct d_uuid		ukey;
	struct cont_df_args	args;
	int			rc;

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);

	d_iov_set(&key, &ukey, sizeof(struct d_uuid));
	d_iov_set(&value, &args, sizeof(struct cont_df_args));
	uuid_clear(it_entry->ie_couuid);

	rc = dbtree_iter_fetch(co_iter->cot_hdl, &key, &value, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching co info: %d\n", rc);
		return rc;
	}
	D_ASSERT(value.iov_len == sizeof(struct cont_df_args));
	uuid_copy(it_entry->ie_couuid, args.ca_cont_df->cd_id);
	it_entry->ie_child_type = VOS_ITER_OBJ;

	return rc;
}

static int
cont_iter_next(struct vos_iterator *iter)
{
	struct cont_iterator	*co_iter = vos_iter2co_iter(iter);

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);
	return dbtree_iter_next(co_iter->cot_hdl);
}

static int
cont_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct cont_iterator	*co_iter = vos_iter2co_iter(iter);
	dbtree_probe_opc_t	opc;

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	/* The container tree will not be affected by the iterator intent,
	 * just set it as DAOS_INTENT_DEFAULT.
	 */
	return dbtree_iter_probe(co_iter->cot_hdl, opc, DAOS_INTENT_DEFAULT,
				 NULL, anchor);
}

static int
cont_iter_delete(struct vos_iterator *iter, void *args)
{
	struct cont_iterator	*co_iter = vos_iter2co_iter(iter);
	int			rc  = 0;

	D_ASSERT(iter->it_type == VOS_ITER_COUUID);
	rc = vos_tx_begin(co_iter->cot_pool);
	if (rc != 0)
		goto failed;

	rc = dbtree_iter_delete(co_iter->cot_hdl, args);

	rc = vos_tx_end(co_iter->cot_pool, rc);
failed:
	if (rc != 0)
		D_ERROR("Failed to delete oid entry: %d\n", rc);
	return rc;
}

struct vos_iter_ops vos_cont_iter_ops = {
	.iop_prepare = cont_iter_prep,
	.iop_finish  = cont_iter_fini,
	.iop_probe   = cont_iter_probe,
	.iop_next    = cont_iter_next,
	.iop_fetch   = cont_iter_fetch,
	.iop_delete  = cont_iter_delete,
};
