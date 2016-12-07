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
 *
 * Handle hash wrappers and callbacks used in VOS
 * vos/vos_hhash.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define DD_SUBSYS	DD_FAC(vos)

#include <daos/common.h>
#include <daos/hash.h>
#include <daos/list.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <vos_hhash.h>

void
vos_co_uhash_free(struct daos_ulink *ulink)
{
	struct vc_hdl *co_hdl;

	co_hdl = vos_ulink2coh(ulink);
	D_FREE_PTR(co_hdl);
}

void
vos_pool_uhash_free(struct daos_ulink *ulink)
{
	struct vp_hdl		*vpool;

	vpool	 = vos_ulink2poh(ulink);
	if (vpool->vp_ph)
		vos_pmemobj_close(vpool->vp_ph);
	if (vpool->vp_fpath != NULL)
		free(vpool->vp_fpath);

	D_FREE_PTR(vpool);
}

struct daos_ulink_ops   co_hdl_uh_ops = {
	.uop_free       = vos_co_uhash_free,
};

struct daos_ulink_ops   vpool_uh_ops = {
	.uop_free       = vos_pool_uhash_free,
};


struct dhash_table*
vos_get_hr_hash()
{
#ifdef VOS_STANDALONE
	return vsa_imems_inst->vis_hr_hash;
#else
	return vos_tls_get()->vtl_imems_inst.vis_hr_hash;
#endif
}

void
vos_pool_handle_init(struct vp_hdl *vpool)
{
	daos_uhash_ulink_init(&vpool->vp_uhlink,
			      &vpool_uh_ops);
}

int
vos_pool_insert_handle(struct vp_hdl *vpool, struct daos_uuid *key,
		       daos_handle_t *poh)
{
	int	rc = 0;

	D_ASSERT(vpool != NULL);
	D_ASSERT(poh != NULL);

	vos_pool_handle_init(vpool);
	rc = daos_uhash_link_insert(vos_get_hr_hash(), key,
				    &vpool->vp_uhlink);

	if (rc) {
		D_ERROR("UHASH table pool insert failed\n");
		D_GOTO(exit, rc);
	}
	*poh = vos_pool2hdl(vpool);

exit:
	return rc;
}

int
vos_pool_lookup_handle(struct daos_uuid *key, struct vp_hdl **vpool)
{
	int			rc = 0;
	struct daos_ulink	*ulink;

	ulink = daos_uhash_link_lookup(vos_get_hr_hash(), key);
	if (ulink != NULL)
		*vpool = vos_ulink2poh(ulink);
	else
		rc = -DER_NONEXIST;

	return rc;
}

void
vos_pool_addref_handle(struct vp_hdl *vpool)
{
	daos_uhash_link_addref(vos_get_hr_hash(), &vpool->vp_uhlink);
}

void
vos_pool_putref_handle(struct vp_hdl *vpool)
{
	daos_uhash_link_putref(vos_get_hr_hash(), &vpool->vp_uhlink);
}

int
vos_pool_release_handle(struct vp_hdl *vpool)
{
	int rc = 0;

	daos_uhash_link_putref(vos_get_hr_hash(), &vpool->vp_uhlink);
	if (daos_uhash_link_last_ref(&vpool->vp_uhlink)) {
		rc = dbtree_close(vpool->vp_ct_hdl);
		if (rc) {
			D_ERROR("Closing btree open handle: %d\n", rc);
			return rc;
		}
		rc = vos_cookie_index_destroy(vpool->vp_ck_hdl);
		if (rc) {
			D_ERROR("Destroying btr handle for cookie index :%d\n",
				rc);
			return rc;
		}
		daos_uhash_link_delete(vos_get_hr_hash(), &vpool->vp_uhlink);
	}
	return rc;
}

void
vos_co_handle_init(struct vc_hdl *co_hdl)
{
	daos_uhash_ulink_init(&co_hdl->vc_uhlink,
			      &co_hdl_uh_ops);
}

int
vos_co_insert_handle(struct vc_hdl *co_hdl, struct daos_uuid *key,
		     daos_handle_t *coh)
{
	int	rc = 0;

	D_ASSERT(co_hdl != NULL && coh != NULL);

	vos_co_handle_init(co_hdl);
	rc = daos_uhash_link_insert(vos_get_hr_hash(), key,
				    &co_hdl->vc_uhlink);
	if (rc) {
		D_ERROR("UHASH table container handle insert failed\n");
		D_GOTO(exit, rc);
	}

	*coh = vos_co2hdl(co_hdl);
exit:
	return rc;
}

int
vos_co_lookup_handle(struct daos_uuid *key, struct vc_hdl **co_hdl)
{
	int			rc = 0;
	struct daos_ulink	*ulink;

	ulink = daos_uhash_link_lookup(vos_get_hr_hash(),
				       key);
	if (ulink != NULL)
		*co_hdl = vos_ulink2coh(ulink);
	else
		rc = -DER_NONEXIST;

	return rc;
}

void
vos_co_putref_handle(struct vc_hdl *co_hdl)
{
	daos_uhash_link_putref(vos_get_hr_hash(), &co_hdl->vc_uhlink);
}

void
vos_co_addref_handle(struct vc_hdl *co_hdl)
{
	daos_uhash_link_addref(vos_get_hr_hash(), &co_hdl->vc_uhlink);
}

int
vos_co_release_handle(struct vc_hdl *co_hdl)
{
	int rc = 0;

	daos_uhash_link_putref(vos_get_hr_hash(), &co_hdl->vc_uhlink);
	if (daos_uhash_link_last_ref(&co_hdl->vc_uhlink)) {
		rc = dbtree_close(co_hdl->vc_btr_hdl);
		if (rc) {
			D_ERROR("Closing btree open handle: %d\n", rc);
			return rc;
		}
		daos_uhash_link_delete(vos_get_hr_hash(), &co_hdl->vc_uhlink);
	}
	return rc;
}
