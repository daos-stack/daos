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
#include <daos/hash.h>
#include <daos/list.h>
#include <vos_layout.h>
#include <vos_internal.h>

static inline struct vc_hdl*
vos_ulink2coh(struct daos_ulink *ulink)
{
	D_ASSERT(ulink != NULL);
	return container_of(ulink, struct vc_hdl, vc_uhlink);
}

void
vos_co_uhash_free(struct daos_ulink *ulink);

/**
 * Container UUID hash manipulation
 */
void
vos_co_handle_init(struct vc_hdl *co_hdl);

int
vos_co_insert_handle(struct vc_hdl *vc_hdl, struct daos_uuid *key,
		     daos_handle_t *coh);

int
vos_co_lookup_handle(struct daos_uuid *key, struct vc_hdl **co_hdl);

void
vos_co_putref_handle(struct vc_hdl *co_hdl);

void
vos_co_addref_handle(struct vc_hdl *co_hdl);

int
vos_co_release_handle(struct vc_hdl *co_hdl);
/*====================================================*/

#endif
