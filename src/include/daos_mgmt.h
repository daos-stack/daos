/**
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
 * DAOS Management API.
 */

#ifndef __DMG_API_H__
#define __DMG_API_H__

/**
 * Create a pool with "uuid" and "mode".
 *
 * "group" and "targets" passes in the address of each target and the total
 * number of targets. The fault domains among the targets are retrieved
 * automatically from external sources.
 *
 * "device" identifies target devices. "size" specifies target sizes (i.e.,
 * maximum amounts of storage space targets can consume) in bytes. Targets are
 * assumed to share the same "path" and "size".
 *
 * "service", which shall be allocated by the caller, returns the pool service
 * address, comprising of a set of replica addresses.
 *
 * TODO(Johann): DMG to call server-side pool create for EVERY layer.
 */
int
dmg_pool_create(const uuid_t uuid, unsigned int mode, const daos_group_t *group,
		const daos_rank_list_t *targets, const char *device,
		uint64_t size, daos_rank_list_t *service, daos_event_t *event);

/**
 * Destroy a pool with "uuid". If there is at least one connection to this
 * pool, and "force" is zero, then this operation completes with DER_BUSY.
 * Otherwise, the pool is destroyed when the operation completes.
 */
int
dmg_pool_destroy(const uuid_t uuid, int force, daos_event_t *event);

#endif /* __DMG_API_H__ */
