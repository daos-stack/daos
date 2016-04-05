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
 * Server-side API of the DAOS-M layer.
 */

#ifndef __DSM_SRV_H__
#define __DSM_SRV_H__

#include <daos/daos_transport.h>

/*
 * Called by dmg on every storage node belonging to this pool. "path" is the
 * directory under which the VOS and metadata files shall be. "target_uuid"
 * returns the UUID generated for the target on this storage node.
 */
int
dsms_pool_create(const uuid_t pool_uuid, const char *path, uuid_t target_uuid);

/*
 * Called by dmg on a single storage node belonging to this pool after the
 * dsms_pool_create() phase completes. "target_uuids" shall be an array of the
 * target UUIDs returned by the dsms_pool_create() calls. "svc_addrs" returns
 * the ranks of the pool services replicas within "group".
 */
int
dsms_pool_svc_create(const uuid_t pool_uuid, unsigned int uid, unsigned int gid,
		     unsigned int mode, int ntargets, const uuid_t target_uuids,
		     const daos_group_t *group,
		     const daos_rank_list_t *target_addrs, int ndomains,
		     const int *domains, const char *path,
		     daos_rank_list_t *svc_addrs);

/*
 * Called by dmg on the pool service leader to list all pool handles of a pool.
 * Upon successful completion, "buf" returns an array of handle UUIDs if its
 * large enough, while "size" returns the size of all the handle UUIDs assuming
 * "buf" is large enough.
 */
int
dsms_pool_hdl_list(const uuid_t pool_uuid, uuid_t buf, size_t *size);

/*
 * Called by dmg on the pool service leader to evict one or all pool handles of
 * a pool. If "handle_uuid" is NULL, all pool handles of the pool are evicted.
 */
int
dsms_pool_hdl_evict(const uuid_t pool_uuid, const uuid_t handle_uuid);

#endif /* __DSM_SRV_H__ */
