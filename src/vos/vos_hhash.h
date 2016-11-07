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

#ifndef __VOS_HHASH_H__
#define __VOS_HHASH_H__

#include <daos/common.h>
#include <vos_layout.h>
#include <vos_internal.h>

static inline struct vc_hdl*
vos_ulink2coh(struct crt_ulink *ulink)
{
	D_ASSERT(ulink != NULL);
	return container_of(ulink, struct vc_hdl, vc_uhlink);
}

static inline struct vp_hdl*
vos_ulink2poh(struct crt_ulink *ulink)
{
	D_ASSERT(ulink != NULL);
	return	container_of(ulink, struct vp_hdl, vp_uhlink);
}

void
vos_co_uhash_free(struct crt_ulink *ulink);

void
vos_pool_uhash_free(struct crt_ulink *ulink);

/**
 * Getting handle has
 * wrapper for TLS and standalone mode
 */
struct dhash_table *vos_get_hr_hash();

/**
 * Pool UUID hash manipulation
 */
void
vos_pool_handle_init(struct vp_hdl *vp_hdl);

int
vos_pool_insert_handle(struct vp_hdl *vp_hdl, struct crt_uuid *key,
		       daos_handle_t *poh);

int
vos_pool_lookup_handle(struct crt_uuid *key, struct vp_hdl **vpool);

void
vos_pool_addref_handle(struct vp_hdl *vpool);

void
vos_pool_putref_handle(struct vp_hdl *vpool);

int
vos_pool_release_handle(struct vp_hdl *vp_hdl);

/**
 * Container UUID hash manipulation
 */
void
vos_co_handle_init(struct vc_hdl *co_hdl);

int
vos_co_insert_handle(struct vc_hdl *vc_hdl, struct crt_uuid *key,
		     daos_handle_t *coh);

int
vos_co_lookup_handle(struct crt_uuid *key, struct vc_hdl **co_hdl);

void
vos_co_putref_handle(struct vc_hdl *co_hdl);

void
vos_co_addref_handle(struct vc_hdl *co_hdl);

int
vos_co_release_handle(struct vc_hdl *co_hdl);
/*====================================================*/

#endif
