/*
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 *
 * Handle hash wrappers and callbacks used in VOS
 * vos/vos_hhash.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#include <daos/common.h>
#include <daos/hash.h>
#include <daos/list.h>
#include <vos_layout.h>
#include <vos_internal.h>
#include <vos_hhash.h>

void
vos_co_hhash_free(struct daos_hlink *hlink)
{
	struct vc_hdl *co_hdl;

	D_ASSERT(hlink);
	co_hdl = container_of(hlink, struct vc_hdl, vc_hlink);
	D_FREE_PTR(co_hdl);
}

void
vos_pool_hhash_free(struct daos_hlink *hlink)
{
	struct vp_hdl *vpool;

	D_ASSERT(hlink != NULL);
	vpool = container_of(hlink, struct vp_hdl, vp_hlink);

	if (vpool->vp_ph)
		pmemobj_close(vpool->vp_ph);
	if (vpool->vp_fpath != NULL)
		free(vpool->vp_fpath);

	D_FREE_PTR(vpool);
}

struct daos_hlink_ops	co_hdl_hh_ops = {
	.hop_free	= vos_co_hhash_free,
};

struct daos_hlink_ops	vpool_hh_ops = {
	.hop_free	= vos_pool_hhash_free,
};


struct daos_hhash*
vos_get_hhash(void)
{
#ifdef VOS_STANDALONE
	return vsa_imems_inst->vis_hhash;
#else
	return vos_tls_get()->vtl_imems_inst.vis_hhash;
#endif
}

void
vos_pool_hhash_init(struct vp_hdl *vp_hdl)
{
	daos_hhash_hlink_init(&vp_hdl->vp_hlink,
			      &vpool_hh_ops);
}

void
vos_pool_delete_handle(struct vp_hdl *vp_hdl)
{
	struct daos_hlink *hlink = &vp_hdl->vp_hlink;

	D_ASSERT(hlink != NULL);
	daos_hhash_link_delete(vos_get_hhash(), hlink);
}

void
vos_pool_insert_handle(struct vp_hdl *vp_hdl,
		       daos_handle_t *poh)
{
	D_ASSERT(vp_hdl != NULL);
	D_ASSERT(poh != NULL);
	daos_hhash_link_insert(vos_get_hhash(),
			       &vp_hdl->vp_hlink,
			       DAOS_HTYPE_POOL);
	daos_hhash_link_key(&vp_hdl->vp_hlink,
			    &poh->cookie);
}

struct vp_hdl*
vos_pool_lookup_handle(daos_handle_t poh)
{
	struct vp_hdl		*vpool = NULL;
	struct daos_hlink	*hlink = NULL;

	hlink = daos_hhash_link_lookup(vos_get_hhash(),
				       poh.cookie);
	if (!hlink)
		D_ERROR("VOS pool handle lookup error\n");
	else
		vpool = container_of(hlink, struct vp_hdl,
				     vp_hlink);
	return vpool;
}

void
vos_pool_putref_handle(struct vp_hdl *vpool)
{
	D_ASSERT(vpool != NULL);
	daos_hhash_link_putref(vos_get_hhash(),
			       &vpool->vp_hlink);
}

void
vos_co_hhash_init(struct vc_hdl *co_hdl)
{
	daos_hhash_hlink_init(&co_hdl->vc_hlink,
			      &co_hdl_hh_ops);
}

void
vos_co_delete_handle(struct vc_hdl *co_hdl)
{
	struct daos_hlink *hlink = &co_hdl->vc_hlink;

	D_ASSERT(hlink != NULL);
	daos_hhash_link_delete(vos_get_hhash(), hlink);
}


void
vos_co_insert_handle(struct vc_hdl *co_hdl,
		     daos_handle_t *coh)
{
	D_ASSERT(co_hdl != NULL);
	D_ASSERT(coh != NULL);
	daos_hhash_link_insert(vos_get_hhash(),
			       &co_hdl->vc_hlink,
			       DAOS_HTYPE_CO);
	daos_hhash_link_key(&co_hdl->vc_hlink,
			    &coh->cookie);
}

void
vos_co_putref_handle(struct vc_hdl *co_hdl)
{
	D_ASSERT(co_hdl != NULL);
	daos_hhash_link_putref(vos_get_hhash(),
			       &co_hdl->vc_hlink);
}

struct vc_hdl*
vos_co_lookup_handle(daos_handle_t coh)
{
	struct vc_hdl		*co_hdl = NULL;
	struct daos_hlink	*hlink = NULL;

	hlink = daos_hhash_link_lookup(vos_get_hhash(),
				       coh.cookie);
	if (!hlink)
		D_ERROR("vos container handle lookup error\n");
	else
		co_hdl = container_of(hlink, struct vc_hdl,
				      vc_hlink);
	return co_hdl;
}
