/**
 * (C) Copyright 2016 Intel Corporation.
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
 * VOS object table definition
 * vos/vos_cookie_index.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define DD_SUBSYS	DD_FAC(vos)

#include <daos_errno.h>
#include <daos/mem.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <vos_internal.h>
#include <vos_obj.h>
#include <vos_hhash.h>

#define COOKIE_BTREE_ORDER 20

static int
vcoi_hkey_size(struct btr_instance *tins)
{
	return sizeof(struct daos_uuid);
}

static void
vcoi_hkey_gen(struct btr_instance *tins, daos_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct daos_uuid));
	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
vcoi_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
	       daos_iov_t *val_iov, struct btr_record *rec)
{
	TMMID(struct vos_cookie_rec_df)	vce_rec_mmid;
	struct vos_cookie_rec_df	*vce_rec;

	D_ASSERT(key_iov->iov_len == sizeof(struct daos_uuid));
	D_ASSERT(val_iov->iov_len == sizeof(daos_epoch_t));

	vce_rec_mmid = umem_znew_typed(&tins->ti_umm, struct vos_cookie_rec_df);
	if (TMMID_IS_NULL(vce_rec_mmid))
		return -DER_NOMEM;

	vce_rec = umem_id2ptr_typed(&tins->ti_umm, vce_rec_mmid);

	memcpy(&vce_rec->cr_max_epoch, val_iov->iov_buf, sizeof(daos_epoch_t));
	rec->rec_mmid = umem_id_t2u(vce_rec_mmid);
	return 0;
}

static int
vcoi_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	TMMID(struct vos_cookie_rec_df)	vce_rec_mmid;
	struct umem_instance		*umm = &tins->ti_umm;

	vce_rec_mmid = umem_id_u2t(rec->rec_mmid, struct vos_cookie_rec_df);
	umem_free_typed(umm, vce_rec_mmid);

	return 0;
}

static int
vcoi_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_cookie_rec_df *vce_rec;

	D_ASSERT(val_iov != NULL);

	vce_rec = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	memcpy(val_iov->iov_buf, &vce_rec->cr_max_epoch, sizeof(daos_epoch_t));

	return 0;
}

static int
vcoi_rec_update(struct btr_instance *tins, struct btr_record *rec,
	       daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	struct vos_cookie_rec_df *vce_rec;

	D_ASSERT(key_iov != NULL && val_iov != NULL);
	vce_rec = umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	/** Update the max epoch */
	vce_rec->cr_max_epoch	= *(daos_epoch_t *)val_iov->iov_buf;
	return 0;
}

static btr_ops_t vcoi_ops = {
	.to_hkey_size	= vcoi_hkey_size,
	.to_hkey_gen	= vcoi_hkey_gen,
	.to_rec_alloc	= vcoi_rec_alloc,
	.to_rec_free	= vcoi_rec_free,
	.to_rec_fetch	= vcoi_rec_fetch,
	.to_rec_update	= vcoi_rec_update,
};


int
vos_cookie_itab_init()
{
	int	rc;

	D_DEBUG(DB_MD, "Registering tree class for cookie table: %d\n",
		VOS_BTR_COOKIE);

	rc = dbtree_class_register(VOS_BTR_COOKIE, 0, &vcoi_ops);
	if (rc)
		D_ERROR("dbtree create failed\n");
	return rc;
}

int
vos_cookie_itab_create(struct umem_attr *uma, struct vos_cookie_itab *itab,
		       daos_handle_t *cookie_handle)
{
	int	rc;

	D_ASSERT(itab->cit_btr.tr_class == 0);
	D_DEBUG(DB_MD, "Create cookie tree in-place :%d\n", VOS_BTR_COOKIE);

	rc = dbtree_create_inplace(VOS_BTR_COOKIE, 0, COOKIE_BTREE_ORDER, uma,
				   &itab->cit_btr, cookie_handle);
	if (rc) {
		D_ERROR("dbtree create failed: %d\n", rc);
		D_GOTO(exit, rc);
	}
exit:
	return rc;

}

int
vos_cookie_itab_destroy(daos_handle_t cih)
{
	int			rc  = 0;

	rc = dbtree_destroy(cih);
	if (rc)
		D_ERROR("COOKIE BTREE destroy failed\n");

	return rc;
}

/**
 * Find the cookie by cookie id and return max epoch,
 * (or) update the max_epoch for the cookie based on update flag
 * or if no cookie exists, add one to the cookie index
 */
int
vos_cookie_find_update(daos_handle_t cih, uuid_t cookie, daos_epoch_t epoch,
		       bool update_flag, daos_epoch_t *epoch_ret)
{
	daos_epoch_t		max_epoch;
	daos_iov_t		key;
	daos_iov_t		value;
	struct daos_uuid	uuid_key;
	int			rc;

	uuid_copy(uuid_key.uuid, cookie);
	daos_iov_set(&key, &uuid_key, sizeof(struct daos_uuid));
	daos_iov_set(&value, &max_epoch, sizeof(daos_epoch_t));

	rc = dbtree_lookup(cih, &key, &value);
	if (rc == 0) {
		D_DEBUG(DB_TRACE, "dbtree lookup found "DF_UUID","DF_U64"\n",
			DP_UUID(cookie), max_epoch);

		if (!update_flag) /* read-only */
			D_GOTO(exit, rc);

		if (epoch <= max_epoch) /* no need to overwrite */
			D_GOTO(exit, rc);
		/* overwrite */

	} else if (rc == -DER_NONEXIST) { /* not found */
		if (!update_flag)
			D_GOTO(exit, rc);
		/* insert */

	} else { /* other failures */
		D_GOTO(exit, rc);
	}

	/** if not found or max_epoch < epoch, update */
	max_epoch = epoch;
	rc = dbtree_update(cih, &key, &value);
	if (rc)
		D_ERROR("Updating the cookie entry\n");
exit:
	if (rc == 0 && epoch_ret != NULL)
		*epoch_ret = max_epoch;
	return rc;
}
