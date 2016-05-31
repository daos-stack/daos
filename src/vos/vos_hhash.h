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

/**
 * Getting handle hash
 * Wrapper for TLS and standalone mode
 */
struct daos_hhash *vos_get_hhash(void);

/**
 * Pool hash manipulation
 */
void
vos_pool_hhash_init(struct vp_hdl *vp_hdl);
void
vos_pool_insert_handle(struct vp_hdl *vp_hdl, daos_handle_t *poh);

struct vp_hdl*
vos_pool_lookup_handle(daos_handle_t poh);

void
vos_pool_putref_handle(struct vp_hdl *vpool);

void
vos_pool_delete_handle(struct vp_hdl *vp_hdl);

/**
 * Container hash manipulation
 */
void
vos_co_hhash_init(struct vc_hdl *co_hdl);
void
vos_co_insert_handle(struct vc_hdl *co_hdl, daos_handle_t *coh);

void
vos_co_putref_handle(struct vc_hdl *co_hdl);

struct vc_hdl*
vos_co_lookup_handle(daos_handle_t coh);

void
vos_co_delete_handle(struct vc_hdl *vc_hdl);

void
vos_co_hhash_free(struct daos_hlink *hlink);

void
vos_pool_hhash_free(struct daos_hlink *hlink);

#endif
