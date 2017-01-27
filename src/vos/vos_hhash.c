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

struct daos_ulink_ops   co_hdl_uh_ops = {
	.uop_free       = vos_co_uhash_free,
};

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
	rc = daos_uhash_link_insert(vos_cont_hhash_get(), key,
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

	ulink = daos_uhash_link_lookup(vos_cont_hhash_get(), key);
	if (ulink != NULL)
		*co_hdl = vos_ulink2coh(ulink);
	else
		rc = -DER_NONEXIST;

	return rc;
}

void
vos_co_putref_handle(struct vc_hdl *co_hdl)
{
	daos_uhash_link_decref(vos_cont_hhash_get(), &co_hdl->vc_uhlink);
}

void
vos_co_addref_handle(struct vc_hdl *co_hdl)
{
	daos_uhash_link_addref(vos_cont_hhash_get(), &co_hdl->vc_uhlink);
}

int
vos_co_release_handle(struct vc_hdl *co_hdl)
{
	int rc = 0;

	daos_uhash_link_decref(vos_cont_hhash_get(), &co_hdl->vc_uhlink);
	if (daos_uhash_link_last_ref(&co_hdl->vc_uhlink)) {
		rc = dbtree_close(co_hdl->vc_btr_hdl);
		if (rc) {
			D_ERROR("Closing btree open handle: %d\n", rc);
			return rc;
		}
		daos_uhash_link_delete(vos_cont_hhash_get(),
				       &co_hdl->vc_uhlink);
	}
	return rc;
}
